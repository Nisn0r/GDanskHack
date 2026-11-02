/***************************************************************************//**
 * @file power_cycle_reset.c
 * @brief Factory reset by repeated power cycles. See power_cycle_reset.h.
 *
 * Counter kept in NVM3 under a user-domain key (the Zigbee stack and the ZCL
 * attribute tokens live in NVM3KEY_DOMAIN_ZIGBEE). Only power-on and
 * brown-out resets count; any other reset (software, OTA, debugger, fault)
 * clears the counter, so reboots never add up to a reset by accident.
 ******************************************************************************/

#include "app/framework/include/af.h"
#include "nvm3_default.h"
#include "power_cycle_reset.h"

#define NVM3KEY_POWER_CYCLE_COUNT  (0x00000U | 0x0F01U)   /* NVM3KEY_DOMAIN_USER */

#define BLINK_HALF_PERIOD_MS  300u
#define BLINK_TOGGLES         6u        /* 3 blinks */
#define LEAVE_TIMEOUT_MS      3000u

typedef enum {
  STATE_IDLE,
  STATE_BLINK,
  STATE_LEAVING,
} reset_state_t;

static sl_zigbee_af_event_t window_event;
static sl_zigbee_af_event_t reset_event;
static reset_state_t state = STATE_IDLE;
static uint8_t toggles;

static uint8_t count_read(void)
{
  uint8_t count = 0;
  if (nvm3_readData(nvm3_defaultHandle, NVM3KEY_POWER_CYCLE_COUNT,
                    &count, sizeof(count)) != SL_STATUS_OK) {
    count = 0;
  }
  return count;
}

static void count_write(uint8_t count)
{
  /* Skip redundant writes: this runs on every boot. */
  if (count_read() != count) {
    nvm3_writeData(nvm3_defaultHandle, NVM3KEY_POWER_CYCLE_COUNT,
                   &count, sizeof(count));
  }
}

static void wipe_and_reboot(void)
{
  /* Network keys, tables, ZCL attributes (incl. StartUp*), this counter. */
  nvm3_eraseAll(nvm3_defaultHandle);
  halReboot();
}

/* Device stayed powered long enough: this was not a reset sequence. */
static void window_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  count_write(0);
}

static void reset_event_handler(sl_zigbee_af_event_t *event)
{
  switch (state) {
    case STATE_BLINK:
      if (toggles < BLINK_TOGGLES) {
        power_cycle_reset_light_override(true, (toggles & 1u) == 0u);
        toggles++;
        sl_zigbee_af_event_set_delay_ms(event, BLINK_HALF_PERIOD_MS);
        return;
      }
      if (sl_zigbee_af_network_state() != SL_ZIGBEE_NO_NETWORK) {
        /* Tell the coordinator we're going, so it drops us from its tables. */
        state = STATE_LEAVING;
        if (sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION) == SL_STATUS_OK) {
          sl_zigbee_af_event_set_delay_ms(event, LEAVE_TIMEOUT_MS);
          return;
        }
      }
      wipe_and_reboot();
      break;

    case STATE_LEAVING:   /* NETWORK_DOWN never came: wipe anyway. */
      wipe_and_reboot();
      break;

    default:
      break;
  }
}

bool power_cycle_reset_in_progress(void)
{
  return state != STATE_IDLE;
}

void power_cycle_reset_stack_status(sl_status_t status)
{
  if (state == STATE_LEAVING && status == SL_STATUS_NETWORK_DOWN) {
    wipe_and_reboot();
  }
}

void power_cycle_reset_init(void)
{
  sl_zigbee_af_event_init(&window_event, window_event_handler);
  sl_zigbee_af_event_init(&reset_event, reset_event_handler);

  uint8_t reason = halGetResetInfo();
  uint8_t count = count_read();

#if defined(POWER_CYCLE_RESET_TEST_COUNT_ANY_RESET)
  /* Bench test only: debugger resets count as power cycles. */
  (void)reason;
  if (false) {
#else
  if (reason != RESET_POWERON && reason != RESET_BROWNOUT) {
#endif
    count_write(0);
    return;
  }

  if (count < UINT8_MAX) {
    count++;
  }
  count_write(count);

  if (count >= POWER_CYCLE_RESET_COUNT) {
    /* The counter stays >= threshold until NVM3 is wiped: if power is cut
     * during the sequence, the next power-up resumes the reset. */
    state = STATE_BLINK;
    toggles = 0;
    sl_zigbee_af_event_set_active(&reset_event);
  } else {
    sl_zigbee_af_event_set_delay_ms(&window_event, POWER_CYCLE_WINDOW_MS);
  }
}
