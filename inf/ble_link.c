/*
 * FlipBase BLE link (Flipper side). See ble_link.h.
 *
 * Uses the firmware's BLE serial profile (the same one the Flipper phone app talks to):
 * the PC writes to the RX characteristic, we notify on TX. Frames are split into 20-byte
 * notifications so they fit even if the PC never raised the ATT MTU.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "ble_link.h"

#include <bt/bt_service/bt.h>
#include <furi_hal.h>
#include <profiles/serial_profile.h>
#include <stdlib.h>
#include <string.h>

#define BLE_KEYS_PATH "/ext/apps_data/flipbase/.ble_serial.keys"
#define RX_BUFFER     2048 /* one 1 KB Skylanders frame (1030 bytes) must fit */
#define TX_CHUNK      20
#define TX_GAP_MS     30 /* TX is indicate-only in stock firmware: each piece must be confirmed first */
#define TX_FRAME_GAP_MS 40 /* minimum pause between two frames sent back to back */
#define RX_IDLE_MS    1000

struct BleLink {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    FuriStreamBuffer* rx;
    BleProtoParser parser;
    uint8_t tx[BLE_PROTO_MAX_FRAME];
    volatile bool connected; /* the Bluetooth status callback says a PC is connected */
    volatile bool rx_seen;   /* the PC has sent us bytes on this connection (so it is there) */
    bool was_connected;
    uint32_t last_rx_tick;
    uint32_t last_tx_tick;
    uint32_t last_rearm_tick;
    volatile uint32_t rx_bytes;
    uint32_t tx_ok;
    uint32_t tx_fail;
};

/* Runs on the Bluetooth thread: only queue the bytes. Returns the free space so the
 * stack can throttle the sender. */
static uint16_t ble_link_event_callback(SerialServiceEvent event, void* context) {
    BleLink* link = context;
    if(event.event == SerialServiceEventTypeDataReceived) {
        link->rx_seen = true;
        link->rx_bytes += event.data.size;
        furi_stream_buffer_send(link->rx, event.data.buffer, event.data.size, 0);
    }
    return (uint16_t)furi_stream_buffer_spaces_available(link->rx);
}

static void ble_link_status_callback(BtStatus status, void* context) {
    BleLink* link = context;
    link->connected = (status == BtStatusConnected);
    if(!link->connected) link->rx_seen = false;
}

BleLink* ble_link_start(void) {
    BleLink* link = malloc(sizeof(BleLink));
    memset(link, 0, sizeof(BleLink));
    ble_proto_init(&link->parser);
    link->rx = furi_stream_buffer_alloc(RX_BUFFER, 1);

    link->bt = furi_record_open(RECORD_BT);
    bt_disconnect(link->bt); /* drop whatever profile/connection was active */
    furi_delay_ms(200);
    bt_set_status_changed_callback(link->bt, ble_link_status_callback, link);
    /* Own key file so pairing with the PC does not disturb the phone's pairing. */
    bt_keys_storage_set_storage_path(link->bt, BLE_KEYS_PATH);

    link->profile = bt_profile_start(link->bt, ble_profile_serial, NULL);
    if(!link->profile) {
        bt_keys_storage_set_default_path(link->bt);
        bt_set_status_changed_callback(link->bt, NULL, NULL);
        furi_record_close(RECORD_BT);
        furi_stream_buffer_free(link->rx);
        free(link);
        return NULL;
    }
    ble_profile_serial_set_event_callback(link->profile, RX_BUFFER, ble_link_event_callback, link);
    furi_hal_bt_start_advertising();
    link->last_rx_tick = furi_get_tick();
    return link;
}

void ble_link_stop(BleLink* link) {
    if(!link) return;
    ble_profile_serial_set_event_callback(link->profile, 0, NULL, NULL);
    bt_set_status_changed_callback(link->bt, NULL, NULL);
    bt_disconnect(link->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(link->bt);
    bt_profile_restore_default(link->bt);
    furi_record_close(RECORD_BT);
    furi_stream_buffer_free(link->rx);
    free(link);
}

bool ble_link_connected(const BleLink* link) {
    return link && (link->connected || link->rx_seen);
}

void ble_link_pump(BleLink* link, BleProtoHandler handler, void* ctx) {
    if(!link) return;

    /* A fresh connection must not inherit half a frame from the last one. */
    if(link->connected != link->was_connected) {
        link->was_connected = link->connected;
        /* Do not throw away bytes the PC already sent if they beat the status callback. */
        if(!(link->connected && link->rx_seen)) {
            ble_proto_init(&link->parser);
            furi_stream_buffer_reset(link->rx);
        }
        link->last_rx_tick = furi_get_tick();
        link->last_rearm_tick = furi_get_tick() - 500; /* first re-registration in ~0.5 s */
    }

    /* The firmware's Bluetooth service claims the serial link for the phone app's RPC session
     * when a device connects, replacing our receive handler (symptom: the PC's writes are
     * accepted but we receive nothing). Register ours again after the connection is up, and
     * keep doing so until bytes actually reach us. */
    if(link->connected && link->rx_bytes == 0 &&
       furi_get_tick() - link->last_rearm_tick > 1000) {
        ble_profile_serial_set_event_callback(
            link->profile, RX_BUFFER, ble_link_event_callback, link);
        link->last_rearm_tick = furi_get_tick();
    }

    uint8_t chunk[64];
    size_t got;
    while((got = furi_stream_buffer_receive(link->rx, chunk, sizeof(chunk), 0)) > 0) {
        link->last_rx_tick = furi_get_tick();
        ble_proto_feed(&link->parser, chunk, got, handler, ctx);
    }

    /* Quiet with a partial frame buffered: stop waiting on a false start marker. */
    if(ble_proto_busy(&link->parser) && furi_get_tick() - link->last_rx_tick > RX_IDLE_MS) {
        ble_proto_resync(&link->parser, handler, ctx);
        link->last_rx_tick = furi_get_tick();
    }
}

bool ble_link_send(BleLink* link, uint8_t type, const uint8_t* payload, uint16_t len) {
    if(!ble_link_connected(link) || len > BLE_PROTO_MAX_PAYLOAD) return false;
    /* RESULT then STATUS are sent back to back; the stack can refuse a second indication until
     * the first is confirmed, so leave a pause between frames. */
    const uint32_t since = furi_get_tick() - link->last_tx_tick;
    if(link->last_tx_tick != 0 && since < TX_FRAME_GAP_MS) furi_delay_ms(TX_FRAME_GAP_MS - since);
    const size_t total = ble_proto_encode(type, payload, len, link->tx);
    for(size_t offset = 0; offset < total; offset += TX_CHUNK) {
        const size_t take = (total - offset < TX_CHUNK) ? (total - offset) : TX_CHUNK;
        if(ble_profile_serial_tx(link->profile, link->tx + offset, (uint16_t)take)) {
            link->tx_ok++;
        } else {
            link->tx_fail++;
        }
        if(offset + take < total) furi_delay_ms(TX_GAP_MS);
        if(!ble_link_connected(link)) return false; /* PC went away mid-frame */
    }
    link->last_tx_tick = furi_get_tick();
    return true;
}

void ble_link_stats(const BleLink* link, uint32_t* rx_bytes, uint32_t* tx_ok, uint32_t* tx_fail) {
    *rx_bytes = link ? link->rx_bytes : 0;
    *tx_ok = link ? link->tx_ok : 0;
    *tx_fail = link ? link->tx_fail : 0;
}
