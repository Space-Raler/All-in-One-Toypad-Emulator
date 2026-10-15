/*
 * FlipBase BLE link (Flipper side): a Bluetooth LE serial connection to the PC "base" app.
 * The PC sends figures; this module hands the bytes to the frame parser in ble_proto.c.
 *
 * Only the Flipper build uses this file (it needs the Bluetooth service).
 * Everything protocol-related lives in ble_proto.c so it can be tested on a PC.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <furi.h>
#include <stdbool.h>

#include "ble_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BleLink BleLink;

/* Switch the Flipper's Bluetooth to the serial profile and start advertising.
 * Any phone/PC already connected to the Flipper is dropped. NULL on failure. */
BleLink* ble_link_start(void);

/* Restore the normal Bluetooth profile and free everything. */
void ble_link_stop(BleLink* link);

/* A PC is connected right now. */
bool ble_link_connected(const BleLink* link);

/* Feed received bytes to the parser; handler runs on the calling thread, once per frame.
 * Call this often from the app's main loop. */
void ble_link_pump(BleLink* link, BleProtoHandler handler, void* ctx);

/* Send one frame to the PC. False when no PC is connected. Blocks briefly (it paces the
 * chunks) so call it from the app thread, never from an interrupt or the BLE callback. */
bool ble_link_send(BleLink* link, uint8_t type, const uint8_t* payload, uint16_t len);

/* Debug counters: bytes received from the PC, radio sends that worked / failed. */
void ble_link_stats(const BleLink* link, uint32_t* rx_bytes, uint32_t* tx_ok, uint32_t* tx_fail);

#ifdef __cplusplus
}
#endif
