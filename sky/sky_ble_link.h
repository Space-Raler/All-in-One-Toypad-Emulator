/*
 * Skylanders BLE transport (Flipper side).
 * Ported from Disney Infinity ble_link.c.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <furi.h>
#include <stdbool.h>
#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>
#include "sky_ble_bridge.h"

#define RX_BUFFER     2048
/* Bytes per notification. The Flipper mobile app sends full-size packets and waits for the radio's
 * "data sent" event; 20-byte packets made a 1 KB frame 52 notifications. 128 needs a link MTU of
 * at least 131, which BlueZ (Linux) and Android 14+ negotiate automatically. If frames arrive
 * garbled on your phone/PC, set this back to 20. */
#define TX_CHUNK      128
#define TX_SENT_WAIT_MS 100 /* longest wait for the radio to report a packet as sent */
#define TX_FLAG_SENT  1
#define TX_GAP_MS     35
#define TX_FRAME_GAP_MS 100
#define RX_IDLE_MS    1000
#define TX_RETRY_MS   40   /* radio buffer full: wait this long, then retry the same chunk */
#define TX_RETRY_MAX  80   /* about 2 s per chunk before the send is given up */

typedef struct SkyBleLink {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    FuriStreamBuffer* rx;
    uint8_t tx[BLE_PROTO_MAX_FRAME];
    volatile bool connected;
    volatile bool rx_seen;
    bool was_connected;
    uint32_t last_rx_tick;
    uint32_t last_tx_tick;
    uint32_t last_rearm_tick;
    volatile uint32_t rx_bytes;
    FuriEventFlag* tx_flag; /* set by the radio when it has sent data and has room again */
    uint16_t tx_retries; /* chunks the radio refused at first (diagnostics, shown on screen) */
} SkyBleLink;

SkyBleLink* sky_ble_link_start(void);
void sky_ble_link_stop(SkyBleLink* link);
bool sky_ble_link_connected(const SkyBleLink* link);
void sky_ble_link_pump(SkyBleLink* link, SkyBleBridge* bridge);
/* Same, for a different protocol handler (LEGO Dimensions): bytes go to feed(), and resync() is
 * called when the stream goes quiet with a partial frame buffered. */
typedef void (*SkyBleLinkFeedFn)(void* ctx, const uint8_t* data, size_t size);
typedef void (*SkyBleLinkResyncFn)(void* ctx);
void sky_ble_link_pump_ex(SkyBleLink* link, SkyBleLinkFeedFn feed, SkyBleLinkResyncFn resync, void* ctx);
/* sky_ble_link_send is declared in sky_ble_bridge.h (included above). */
