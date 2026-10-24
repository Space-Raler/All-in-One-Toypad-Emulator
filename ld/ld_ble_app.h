/*
 * LEGO Dimensions: Bluetooth link to the web page (Flipper glue around ld_ble.c).
 * One link per app run. Everything runs on the app's own thread (the view dispatcher tick),
 * the same thread that handles the Flipper's buttons, so the pad is never changed by two
 * threads at once.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turn the Bluetooth serial link on / off. Start returns false if the radio could not be taken. */
bool ld_ble_app_start(void);
void ld_ble_app_stop(void);
bool ld_ble_app_enabled(void);
/* Short text for the Config screen: "Waiting for PC" or "PC connected". */
const char* ld_ble_app_status_text(void);
/* Call about every 100 ms. Returns true when the web page changed the pad (redraw the screen). */
bool ld_ble_app_tick(void);
/* True once after the web page saved a vehicle on the Flipper (refresh the saved-vehicles menu). */
bool ld_ble_app_take_saved_changed(void);

#ifdef __cplusplus
}
#endif
