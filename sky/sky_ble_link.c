#include "sky_ble_link.h"
#include <stdlib.h>
#include <string.h>
#include <furi_hal.h>

static uint16_t sky_ble_link_event_callback(SerialServiceEvent event, void* context) {
    SkyBleLink* link = context;
    if(event.event == SerialServiceEventTypeDataReceived) {
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
    SkyBleLink* link = malloc(sizeof(SkyBleLink));
    memset(link, 0, sizeof(SkyBleLink));
    link->rx = furi_stream_buffer_alloc(RX_BUFFER, 1);

    link->bt = furi_record_open(RECORD_BT);
    bt_disconnect(link->bt);
    furi_delay_ms(200);
    bt_set_status_changed_callback(link->bt, sky_ble_link_status_callback, link);

    link->profile = bt_profile_start(link->bt, ble_profile_serial, NULL);
    if(!link->profile) {
        furi_record_close(RECORD_BT);
        furi_stream_buffer_free(link->rx);
        free(link);
        return NULL;
    }
    ble_profile_serial_set_event_callback(link->profile, RX_BUFFER, sky_ble_link_event_callback, link);
    furi_hal_bt_start_advertising();
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
    free(link);
}

bool sky_ble_link_connected(const SkyBleLink* link) {
    return link && (link->connected || link->rx_seen);
}

void sky_ble_link_pump(SkyBleLink* link, SkyBleBridge* bridge) {
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
        sky_ble_bridge_pump(bridge, chunk, got);
    }

    /* Quiet with a partial frame buffered: stop waiting on a false start marker. */
    if(furi_get_tick() - link->last_rx_tick > RX_IDLE_MS) {
        sky_ble_bridge_resync(bridge);
        link->last_rx_tick = furi_get_tick();
    }
}

bool sky_ble_link_send(SkyBleLink* link, uint8_t type, const uint8_t* payload, uint16_t len) {
    if(!sky_ble_link_connected(link) || len > BLE_PROTO_MAX_PAYLOAD) return false;

    uint8_t frame[BLE_PROTO_MAX_FRAME];
    const size_t total = ble_proto_encode(type, payload, len, frame);

    for(size_t offset = 0; offset < total; offset += TX_CHUNK) {
        const size_t take = (total - offset < TX_CHUNK) ? (total - offset) : TX_CHUNK;
        if(!ble_profile_serial_tx(link->profile, frame + offset, (uint16_t)take)) {
            return false;
        }
        if(offset + take < total) furi_delay_ms(TX_GAP_MS);
    }
    return true;
}
