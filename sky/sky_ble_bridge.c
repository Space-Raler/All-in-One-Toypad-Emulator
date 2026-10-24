/* Skylanders BLE bridge. See sky_ble_bridge.h. SPDX-License-Identifier: GPL-2.0-only */
#include "sky_ble_bridge.h"

#include <stdlib.h>
#include <string.h>

static bool bridge_send_cb(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len) {
    return sky_ble_bridge_send(ctx, type, payload, len);
}

SkyBleBridge* sky_ble_bridge_alloc(SkyBase* base) {
    SkyBleBridge* bridge = malloc(sizeof(SkyBleBridge));
    if(!bridge) return NULL;
    memset(bridge, 0, sizeof(*bridge));
    ble_proto_init(&bridge->parser);
    sky_ble_init(&bridge->ble, base, bridge_send_cb, bridge);
    return bridge;
}

void sky_ble_bridge_free(SkyBleBridge* bridge) {
    if(bridge) sky_ble_list_cancel(&bridge->ble);
    free(bridge);
}

void sky_ble_bridge_set_link(SkyBleBridge* bridge, SkyBleLink* link) {
    bridge->ble_link = link;
}

void sky_ble_bridge_configure(
    SkyBleBridge* bridge,
    uint8_t game,
    void (*lock)(void),
    void (*unlock)(void)) {
    bridge->ble.game = game;
    bridge->ble.lock = lock;
    bridge->ble.unlock = unlock;
}

void sky_ble_bridge_set_usb_state(SkyBleBridge* bridge, uint8_t usb_state) {
    bridge->ble.usb_state = usb_state;
}

void sky_ble_bridge_set_file_ops(
    SkyBleBridge* bridge, void* file_ctx, SkyBleListDumpsFn list_dumps,
    SkyBleReadDumpFn read_dump, SkyBleWriteDumpFn write_dump) {
    sky_ble_set_file_ops(&bridge->ble, file_ctx, list_dumps, read_dump, write_dump);
}

void sky_ble_bridge_set_list_ops(
    SkyBleBridge* bridge, SkyBleListBeginFn begin, SkyBleListStepFn step, SkyBleListEndFn end) {
    sky_ble_set_list_ops(&bridge->ble, begin, step, end);
}

void sky_ble_bridge_list_tick(SkyBleBridge* bridge) {
    sky_ble_list_tick(&bridge->ble);
}

void sky_ble_bridge_pump(SkyBleBridge* bridge, const uint8_t* data, size_t size) {
    ble_proto_feed(&bridge->parser, data, size, sky_ble_on_message, &bridge->ble);
}

void sky_ble_bridge_resync(SkyBleBridge* bridge) {
    if(ble_proto_busy(&bridge->parser)) {
        ble_proto_resync(&bridge->parser, sky_ble_on_message, &bridge->ble);
    }
}

bool sky_ble_bridge_send(SkyBleBridge* bridge, uint8_t type, const uint8_t* payload, uint16_t len) {
    if(!bridge->ble_link) return false;
    return sky_ble_link_send(bridge->ble_link, type, payload, len);
}

void sky_ble_bridge_poll_saves(
    SkyBleBridge* bridge,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx) {
    sky_ble_poll_saves(&bridge->ble, scratch, unsent, unsent_ctx);
}

void sky_ble_bridge_flush_unsent(
    SkyBleBridge* bridge,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx) {
    sky_ble_flush_unsent(&bridge->ble, scratch, unsent, unsent_ctx);
}

bool sky_ble_bridge_slot_remote(const SkyBleBridge* bridge, uint8_t slot) {
    return slot < SKY_SLOT_COUNT && bridge->ble.remote[slot];
}

void sky_ble_bridge_forget_slot(SkyBleBridge* bridge, uint8_t slot) {
    if(slot < SKY_SLOT_COUNT) bridge->ble.remote[slot] = false;
}
