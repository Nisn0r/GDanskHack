/***************************************************************************//**
 * @file power_cycle_reset.h
 * @brief Factory reset by repeated power cycles (no button, no wired access).
 *
 * N consecutive power-ups (POR or brown-out), each shorter than
 * POWER_CYCLE_WINDOW_MS, trigger a factory reset: blink, leave the network
 * (if joined), erase all of NVM3, reboot.
 ******************************************************************************/

#ifndef POWER_CYCLE_RESET_H
#define POWER_CYCLE_RESET_H

#include <stdbool.h>
#include "sl_status.h"

/* Number of short power-ups that trigger the reset. */
#define POWER_CYCLE_RESET_COUNT   6
/* Time the device must stay powered for the sequence to be cancelled. It
 * also bounds the "on" phase of each cycle as seen by the user, plus the
 * time the PSU capacitors keep the chip alive after switch-off. */
#define POWER_CYCLE_WINDOW_MS     3000u

/* Call once from sl_zigbee_af_main_init_cb(), after the light is set up. */
void power_cycle_reset_init(void);

/* True while the blink / leave / wipe sequence runs. */
bool power_cycle_reset_in_progress(void);

/* Hooks the application provides. */
void power_cycle_reset_light_override(bool active, bool on);

/* Forward from sl_zigbee_af_stack_status_cb(). */
void power_cycle_reset_stack_status(sl_status_t status);

#endif // POWER_CYCLE_RESET_H
