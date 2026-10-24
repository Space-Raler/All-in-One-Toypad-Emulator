#include "sky_ble_link.h"
#include <stdlib.h>
#include <string.h>
#include <furi_hal.h>

static uint16_t sky_ble_link_event_callback(SerialServiceEvent event, void* context) {
    SkyBleLink* link = context;
    if(event.event == SerialServiceEventTypeDataSent) {
        if(link->tx_flag) furi_event_flag_set(link->tx_flag, TX_FLAG_SENT);
    } else if(event.event == SerialServiceEventTypeDataReceived) {
        link->rx_seen = true;
        link->rx_bytes += event.data.size;
        furi_stream_buffer_send(link->rx, event.data.buffer, event.data.size, 0);
    }
    return (uint16_t)furi_stream_buffer_spaces_available(link->rx);
}

static void sky_ble_link_status_callback(BtStatus status, void* context) {
    SkyBleLink* link = context;
    link->connected = (status == BtStatusConnected);
    if(!link->connected) link->rx_seen = false;
}

SkyBleLink* sky_ble_link_start(void) {
    FURI_LOG_I("ToyBoxBLE", "start: heap free %u", (unsigned)memmgr_get_free_heap());
    SkyBleLink* link = malloc(sizeof(SkyBleLink));
    memset(link, 0, sizeof(SkyBleLink));
    link->rx = furi_stream_buffer_alloc(RX_BUFFER, 1);
    link->tx_flag = furi_event_flag_alloc();

    FURI_LOG_I("ToyBoxBLE", "opening bt record");
    link->bt = furi_record_open(RECORD_BT);
    FURI_LOG_I("ToyBoxBLE", "bt_disconnect");
    bt_disconnect(link->bt);
    furi_delay_ms(200);
    bt_set_status_changed_callback(link->bt, sky_ble_link_status_callback, link);

    FURI_LOG_I("ToyBoxBLE", "bt_profile_start(serial)");
    link->profile = bt_profile_start(link->bt, ble_profile_serial, NULL);
    FURI_LOG_I("ToyBoxBLE", "bt_profile_start returned %p", (void*)link->profile);
    if(!link->profile) {
        furi_record_close(RECORD_BT);
        furi_stream_buffer_free(link->rx);
        furi_event_flag_free(link->tx_flag);
        free(link);
        return NULL;
    }
    ble_profile_serial_set_event_callback(link->profile, RX_BUFFER, sky_ble_link_event_callback, link);
    FURI_LOG_I("ToyBoxBLE", "start advertising");
    furi_hal_bt_start_advertising();
    FURI_LOG_I("ToyBoxBLE", "started, heap free %u", (unsigned)memmgr_get_free_heap());
    link->last_rx_tick = furi_get_tick();
    return link;
}

void sky_ble_link_stop(SkyBleLink* link) {
    if(!link) return;
    ble_profile_serial_set_event_callback(link->profile, 0, NULL, NULL);
    bt_set_status_changed_callback(link->bt, NULL, NULL);
    bt_disconnect(link->bt);
    furi_delay_ms(200);
    bt_profile_restore_default(link->bt);
    furi_record_close(RECORD_BT);
    furi_stream_buffer_free(link->rx);
    furi_event_flag_free(link->tx_flag);
    free(link);
}

bool sky_ble_link_connected(const SkyBleLink* link) {
    return link && (link->connected || link->rx_seen);
}

static void bridge_feed_cb(void* ctx, const uint8_t* data, size_t size) {
    sky_ble_bridge_pump(ctx, data, size);
}

static void bridge_resync_cb(void* ctx) {
    sky_ble_bridge_resync(ctx);
}

void sky_ble_link_pump(SkyBleLink* link, SkyBleBridge* bridge) {
    sky_ble_link_pump_ex(link, bridge_feed_cb, bridge_resync_cb, bridge);
}

void sky_ble_link_pump_ex(
    SkyBleLink* link,
    SkyBleLinkFeedFn feed,
    SkyBleLinkResyncFn resync,
    void* ctx) {
    if(!link) return;

    if(link->connected != link->was_connected) {
        link->was_connected = link->connected;
        if(!(link->connected && link->rx_seen)) {
            furi_stream_buffer_reset(link->rx);
        }
        link->last_rx_tick = furi_get_tick();
        link->last_rearm_tick = furi_get_tick() - 500;
    }

    if(link->connected && link->rx_bytes == 0 &&
       furi_get_tick() - link->last_rearm_tick > 1000) {
        ble_profile_serial_set_event_callback(
            link->profile, RX_BUFFER, sky_ble_link_event_callback, link);
    }

    uint8_t chunk[64];
    size_t got;
    while((got = furi_stream_buffer_receive(link->rx, chunk, sizeof(chunk), 0)) > 0) {
        link->last_rx_tick = furi_get_tick();
        feed(ctx, chunk, got);
    }

    /* Quiet with a partial frame buffered: stop waiting on a false start marker. */
    if(furi_get_tick() - link->last_rx_tick > RX_IDLE_MS) {
        resync(ctx);
        link->last_rx_tick = furi_get_tick();
    }
}

bool sky_ble_link_send(SkyBleLink* link, uint8_t type, const uint8_t* payload, uint16_t len) {
    if(!sky_ble_link_connected(link) || len > BLE_PROTO_MAX_PAYLOAD) return false;

    /* A dump-list response is many small frames in a row. Without an inter-frame pause,
     * the serial BLE TX queue fills and R (radio chunk retries) climbs continuously. */
    const uint32_t since = furi_get_tick() - link->last_tx_tick;
    if(link->last_tx_tick != 0 && since < TX_FRAME_GAP_MS) {
        furi_delay_ms(TX_FRAME_GAP_MS - since);
    }

    uint8_t frame[BLE_PROTO_MAX_FRAME];
    const size_t total = ble_proto_encode(type, payload, len, frame);

    for(size_t offset = 0; offset < total; offset += TX_CHUNK) {
        const size_t take = (total - offset < TX_CHUNK) ? (total - offset) : TX_CHUNK;
        /* A 1 KB SAVE is 52 notifications. If the phone drains them slower than they are queued
         * the radio refuses a chunk; dropping the rest would leave the PC with half a frame, so
         * wait for room and retry this same chunk. */
        uint16_t tries = 0;
        furi_event_flag_clear(link->tx_flag, TX_FLAG_SENT); /* forget older "sent" signals */
        while(!ble_profile_serial_tx(link->profile, frame + offset, (uint16_t)take)) {
            link->tx_retries++;
            if(++tries >= TX_RETRY_MAX || !sky_ble_link_connected(link)) {
                link->last_tx_tick = furi_get_tick();
                return false;
            }
            /* Queue full: wait until the radio says it has room (not a fixed guess), then retry. */
            furi_event_flag_wait(link->tx_flag, TX_FLAG_SENT, FuriFlagWaitAny, TX_RETRY_MS);
        }
        /* Like the mobile app's RPC sender: let this packet leave before queuing the next. */
        if(offset + take < total) {
            furi_event_flag_wait(link->tx_flag, TX_FLAG_SENT, FuriFlagWaitAny, TX_SENT_WAIT_MS);
        }
    }
    link->last_tx_tick = furi_get_tick();
    return true;
}
