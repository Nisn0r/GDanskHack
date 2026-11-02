/***************************************************************************//**
 * @file printf.h
 * @brief Production stub: printf compiles to nothing.
 *
 * The SDK's slot_manager (needed by the OTA storage) includes "printf.h"
 * without requiring the printf component, which in turn pulls in iostream.
 * This build has no console at all, so its messages are dropped here
 * instead.
 ******************************************************************************/

#ifndef GDANSK_LIGHT_PRINTF_STUB_H
#define GDANSK_LIGHT_PRINTF_STUB_H

#define printf(...) ((void)0)

#endif
