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
#define TX_CHUNK      20
#define TX_GAP_MS     30
#define TX_FRAME_GAP_MS 40
#define RX_IDLE_MS    1000

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
} SkyBleLink;

SkyBleLink* sky_ble_link_start(void);
void sky_ble_link_stop(SkyBleLink* link);
bool sky_ble_link_connected(const SkyBleLink* link);
void sky_ble_link_pump(SkyBleLink* link, SkyBleBridge* bridge);
/* sky_ble_link_send is declared in sky_ble_bridge.h (included above). */
