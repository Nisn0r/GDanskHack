/***************************************************************************//**
 * @file app.c
 * @brief gdansk-light: Zigbee 3.0 color-temperature light (router, mains powered).
 *
 * Endpoint 1 = "Color Temperature Light" (0x010C). The ZCL plugins (on-off,
 * level-control, color-control-server) own the attribute state, transitions
 * and power-on behavior (StartUp* attributes, NVM-backed in the .zap); this
 * file only mirrors the resulting attributes onto two PWM outputs:
 *
 *   PA00  TIMER0 CC0  brightness  duty = OnOff ? CurrentLevel/254 : 0
 *   PA03  TIMER0 CC1  color temp  duty = (max - mireds) / (max - min)
 *                                 0 % = warmest (physical max), 100 % = coldest
 *                                 (CCT_PWM_INVERT, the luminaire's driver)
 *
 * PA03 is also the SWV/TDO debug pin: pwm_init() releases both routes.
 *
 * Production build: no console, no CLI, no debug output (the module is
 * sealed in the luminaire, no debugger access).
 ******************************************************************************/

#include "app/framework/include/af.h"
#include "network-steering.h"
#include "sl_power_manager.h"
#include "power_cycle_reset.h"


#include "em_cmu.h"
#include "em_gpio.h"
#include "em_timer.h"

#define LIGHT_ENDPOINT 1

#define PWM_TIMER           TIMER0
#define PWM_CLOCK           cmuClock_TIMER0
#define PWM_FREQ_HZ         2000u

#define BRIGHTNESS_PORT     gpioPortA
#define BRIGHTNESS_PIN      0
#define BRIGHTNESS_CC       0

#define CCT_PORT            gpioPortA
#define CCT_PIN             3
#define CCT_CC              1
/* 1: 100 % duty = coldest (the luminaire's driver, checked 2026-10-23). */
#define CCT_PWM_INVERT      1

/* Retry interval while not on a network (network steering itself scans all
 * channels with the well-known and install-code keys on each attempt). */
#define REJOIN_RETRY_MS     (60u * 1000u)

#define IDENTIFY_BLINK_MS   500u

/* WDOG0 times out after 2 s and is fed from the stack/app ticks, but the
 * framework sleeps (EM1) until the next event with no watchdog limit: keep
 * an event due every second so an idle lamp still runs its ticks. A hung
 * main loop stops feeding and resets. */
#define WATCHDOG_TICK_MS    1000u

static uint32_t pwm_top;
static sl_zigbee_af_event_t join_event;
static sl_zigbee_af_event_t identify_event;
static sl_zigbee_af_event_t watchdog_event;
static bool identify_active;
static bool identify_phase;
static bool reset_override;
static bool reset_override_on;

// -----------------------------------------------------------------------------
// PWM

static void pwm_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  CMU_ClockEnable(PWM_CLOCK, true);

  GPIO_PinModeSet(BRIGHTNESS_PORT, BRIGHTNESS_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(CCT_PORT, CCT_PIN, gpioModePushPull, 0);

  /* PA03 is also the SWV trace pin and JTAG TDO; either route overrides the
   * timer output. SWV is off in the config (SL_DEBUG_SWO_ENABLE 0), TDO is
   * on from reset. Release both; SWCLK/SWDIO stay enabled for the J-Link. */
  GPIO->TRACEROUTEPEN &= ~GPIO_TRACEROUTEPEN_SWVPEN;
  GPIO->DBGROUTEPEN &= ~GPIO_DBGROUTEPEN_TDOPEN;

  /* Series 2: any pin can be routed to any timer channel. */
  GPIO->TIMERROUTE[0].CC0ROUTE = (BRIGHTNESS_PORT << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT)
                                 | (BRIGHTNESS_PIN << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
  GPIO->TIMERROUTE[0].CC1ROUTE = (CCT_PORT << _GPIO_TIMER_CC1ROUTE_PORT_SHIFT)
                                 | (CCT_PIN << _GPIO_TIMER_CC1ROUTE_PIN_SHIFT);
  GPIO->TIMERROUTE[0].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN | GPIO_TIMER_ROUTEEN_CC1PEN;

  TIMER_InitCC_TypeDef cc = TIMER_INITCC_DEFAULT;
  cc.mode = timerCCModePWM;
  TIMER_InitCC(PWM_TIMER, BRIGHTNESS_CC, &cc);
  TIMER_InitCC(PWM_TIMER, CCT_CC, &cc);

  TIMER_Init_TypeDef init = TIMER_INIT_DEFAULT;
  init.enable = false;
  TIMER_Init(PWM_TIMER, &init);

  pwm_top = CMU_ClockFreqGet(PWM_CLOCK) / PWM_FREQ_HZ;
  TIMER_TopSet(PWM_TIMER, pwm_top);
  TIMER_CompareSet(PWM_TIMER, BRIGHTNESS_CC, 0);
  TIMER_CompareSet(PWM_TIMER, CCT_CC, 0);
  TIMER_Enable(PWM_TIMER, true);
}

/* duty in [0, scale] -> compare value in [0, pwm_top + 1] (100 % = always high). */
static void pwm_set(unsigned int cc, uint32_t duty, uint32_t scale)
{
  uint32_t compare = (uint32_t)(((uint64_t)duty * (pwm_top + 1u)) / scale);
  TIMER_CompareBufSet(PWM_TIMER, cc, compare);
}

// -----------------------------------------------------------------------------
// Attributes -> outputs

static void light_update(void)
{
  bool on = false;
  uint8_t level = 0xFE;
  uint16_t mireds = 250, min_mireds = 153, max_mireds = 370;

  sl_zigbee_af_read_server_attribute(LIGHT_ENDPOINT, ZCL_ON_OFF_CLUSTER_ID,
                                     ZCL_ON_OFF_ATTRIBUTE_ID,
                                     (uint8_t *)&on, sizeof(on));
  sl_zigbee_af_read_server_attribute(LIGHT_ENDPOINT, ZCL_LEVEL_CONTROL_CLUSTER_ID,
                                     ZCL_CURRENT_LEVEL_ATTRIBUTE_ID,
                                     &level, sizeof(level));
  sl_zigbee_af_read_server_attribute(LIGHT_ENDPOINT, ZCL_COLOR_CONTROL_CLUSTER_ID,
                                     ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID,
                                     (uint8_t *)&mireds, sizeof(mireds));
  sl_zigbee_af_read_server_attribute(LIGHT_ENDPOINT, ZCL_COLOR_CONTROL_CLUSTER_ID,
                                     ZCL_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_ATTRIBUTE_ID,
                                     (uint8_t *)&min_mireds, sizeof(min_mireds));
  sl_zigbee_af_read_server_attribute(LIGHT_ENDPOINT, ZCL_COLOR_CONTROL_CLUSTER_ID,
                                     ZCL_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_ATTRIBUTE_ID,
                                     (uint8_t *)&max_mireds, sizeof(max_mireds));

  /* Factory-reset and identify feedback: blink at full brightness,
   * overriding the current state. */
  if (reset_override) {
    on = reset_override_on;
    level = 0xFE;
  } else if (identify_active) {
    on = identify_phase;
    level = 0xFE;
  }
  /* The plugin clamps to 1..254, but a value stored before that (or 0xFF,
   * reserved) must not push the compare value past TOP. */
  if (level > 0xFE) {
    level = 0xFE;
  }
  pwm_set(BRIGHTNESS_CC, on ? level : 0, 0xFE);

  if (max_mireds <= min_mireds) {
    max_mireds = min_mireds + 1;
  }
  if (mireds < min_mireds) {
    mireds = min_mireds;
  } else if (mireds > max_mireds) {
    mireds = max_mireds;
  }
  uint32_t span = max_mireds - min_mireds;
  uint32_t warm = mireds - min_mireds;
  pwm_set(CCT_CC, CCT_PWM_INVERT ? span - warm : warm, span);
}

// -----------------------------------------------------------------------------
// Network: join (never form), retry while orphaned

static void join_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  if (sl_zigbee_af_network_state() == SL_ZIGBEE_NO_NETWORK
      && !power_cycle_reset_in_progress()) {
    (void)sl_zigbee_af_network_steering_start();
  }
}

void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t totalBeacons,
                                               uint8_t joinAttempts,
                                               uint8_t finalState)
{
  (void)totalBeacons;
  (void)joinAttempts;
  (void)finalState;
  if (status != SL_STATUS_OK) {
    sl_zigbee_af_event_set_delay_ms(&join_event, REJOIN_RETRY_MS);
  }
}

void sl_zigbee_af_stack_status_cb(sl_status_t status)
{
  power_cycle_reset_stack_status(status);
  if (status == SL_STATUS_NETWORK_UP) {
    sl_zigbee_af_event_set_inactive(&join_event);
  } else if (status == SL_STATUS_NETWORK_DOWN
             && sl_zigbee_af_network_state() == SL_ZIGBEE_NO_NETWORK) {
    /* We left the network (leave command, factory reset...): look for a new
     * one. A plain parent/coordinator loss keeps the network parameters and
     * the stack rejoins on its own. */
    sl_zigbee_af_event_set_delay_ms(&join_event, 5000);
  }
}

// -----------------------------------------------------------------------------
// Identify feedback

static void identify_event_handler(sl_zigbee_af_event_t *event)
{
  identify_phase = !identify_phase;
  light_update();
  sl_zigbee_af_event_set_delay_ms(event, IDENTIFY_BLINK_MS);
}

void sl_zigbee_af_identify_start_feedback_cb(uint8_t endpoint, uint16_t identifyTime)
{
  (void)identifyTime;
  if (endpoint == LIGHT_ENDPOINT) {
    identify_active = true;
    sl_zigbee_af_event_set_active(&identify_event);
  }
}

void sl_zigbee_af_identify_stop_feedback_cb(uint8_t endpoint)
{
  if (endpoint == LIGHT_ENDPOINT) {
    identify_active = false;
    sl_zigbee_af_event_set_inactive(&identify_event);
    light_update();
  }
}

// -----------------------------------------------------------------------------
// Watchdog: wake the main loop so its ticks feed WDOG0

static void watchdog_event_handler(sl_zigbee_af_event_t *event)
{
  sl_zigbee_af_event_set_delay_ms(event, WATCHDOG_TICK_MS);
}

// -----------------------------------------------------------------------------
// Factory reset feedback (power_cycle_reset.c)

void power_cycle_reset_light_override(bool active, bool on)
{
  reset_override = active;
  reset_override_on = on;
  light_update();
}

// -----------------------------------------------------------------------------
// Framework hooks

void sl_zigbee_af_main_init_cb(void)
{
  /* Mains powered: never go below EM1, TIMER0 PWM stops in EM2. */
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);

  pwm_init();
  light_update();

  power_cycle_reset_init();

  sl_zigbee_af_event_init(&join_event, join_event_handler);
  sl_zigbee_af_event_init(&identify_event, identify_event_handler);
  sl_zigbee_af_event_init(&watchdog_event, watchdog_event_handler);
  sl_zigbee_af_event_set_active(&watchdog_event);
  /* Let the stack restore its network state from NVM3 first. */
  sl_zigbee_af_event_set_delay_ms(&join_event, 100);
}

void sl_zigbee_af_post_attribute_change_cb(uint8_t endpoint,
                                           sl_zigbee_af_cluster_id_t clusterId,
                                           sl_zigbee_af_attribute_id_t attributeId,
                                           uint8_t mask,
                                           uint16_t manufacturerCode,
                                           uint8_t type,
                                           uint8_t size,
                                           uint8_t *value)
{
  (void)attributeId;
  (void)manufacturerCode;
  (void)type;
  (void)size;
  (void)value;

  if (endpoint == LIGHT_ENDPOINT && mask == CLUSTER_MASK_SERVER
      && (clusterId == ZCL_ON_OFF_CLUSTER_ID
          || clusterId == ZCL_LEVEL_CONTROL_CLUSTER_ID
          || clusterId == ZCL_COLOR_CONTROL_CLUSTER_ID)) {
    light_update();
  }
}

/* Called after the plugins applied StartUpOnOff / StartUpCurrentLevel /
 * StartUpColorTemperatureMireds at boot. */
void sl_zigbee_af_on_off_cluster_server_post_init_cb(uint8_t endpoint)
{
  if (endpoint == LIGHT_ENDPOINT) {
    light_update();
  }
}

void sl_zigbee_af_radio_needs_calibrating_cb(void)
{
  sl_mac_calibrate_current_channel();
}
