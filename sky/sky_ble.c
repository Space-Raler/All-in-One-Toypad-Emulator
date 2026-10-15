/* Skylanders BLE link, message handling. See sky_ble.h. SPDX-License-Identifier: GPL-2.0-only */
#include "sky_ble.h"

#include <string.h>

#include "../inf/ble_proto.h"

static void lock(SkyBle* b) {
    if(b->lock) b->lock();
}

static void unlock(SkyBle* b) {
    if(b->unlock) b->unlock();
}

void sky_ble_init(SkyBle* b, SkyBase* base, SkyBleSendFn send, void* send_ctx) {
    memset(b, 0, sizeof(*b));
    b->base = base;
    b->send = send;
    b->send_ctx = send_ctx;
}

static void send_result(SkyBle* b, uint8_t request, uint8_t slot, uint8_t code) {
    const uint8_t payload[3] = {request, slot, code};
    b->send(b->send_ctx, BleMsgResult, payload, sizeof(payload));
}

static void send_status(SkyBle* b) {
    uint16_t mask = 0;
    lock(b);
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(sky_base_slot_present(b->base, i)) mask |= (uint16_t)(1u << i);
    }
    unlock(b);
    const uint8_t payload[5] = {
        BLE_PROTO_VERSION_SKY, b->usb_state, (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8), b->game};
    b->send(b->send_ctx, BleMsgStatus, payload, sizeof(payload));
}

void sky_ble_on_message(void* ctx, uint8_t type, const uint8_t* p, uint16_t len) {
    SkyBle* b = ctx;
    switch(type) {
    case BleMsgHello:
    case BleMsgStatusReq:
        send_status(b);
        break;
    case BleMsgLoad: {
        const uint8_t slot = len ? p[0] : 0;
        if(len != 1 + BLE_PROTO_SKY_FIGURE) {
            send_result(b, type, slot, BleResultBadLength);
        } else if(slot >= SKY_SLOT_COUNT) {
            send_result(b, type, slot, BleResultBadSlot);
        } else {
            int at;
            lock(b);
            if(sky_base_slot_present(b->base, slot)) sky_base_remove_figure(b->base, slot);
            at = sky_base_load_figure_at(b->base, slot, p + 1);
            unlock(b);
            b->remote[slot] = at == slot;
            send_result(b, type, slot, at == slot ? BleResultOk : BleResultUnknown);
            send_status(b);
        }
        break;
    }
    case BleMsgRemove: {
        const uint8_t slot = len ? p[0] : 0;
        if(len != 1 || slot >= SKY_SLOT_COUNT) {
            send_result(b, type, slot, BleResultBadSlot);
        } else {
            lock(b);
            const bool removed = sky_base_remove_figure(b->base, slot);
            unlock(b);
            b->remote[slot] = false;
            send_result(b, type, slot, removed ? BleResultOk : BleResultNotPresent);
            send_status(b);
        }
        break;
    }
    case BleMsgClear:
        lock(b);
        for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
            if(sky_base_slot_present(b->base, i)) sky_base_remove_figure(b->base, i);
        }
        unlock(b);
        memset(b->remote, 0, sizeof(b->remote));
        send_result(b, type, 0, BleResultOk);
        send_status(b);
        break;
    default:
        send_result(b, type, 0, BleResultUnknown);
        break;
    }
}

void sky_ble_poll_saves(
    SkyBle* b,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx) {
    static uint8_t frame[1 + SKY_FIGURE_SIZE];
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(!b->remote[i]) continue;
        lock(b);
        const bool dirty = sky_base_take_dirty(b->base, i, scratch);
        unlock(b);
        if(!dirty) continue;
        frame[0] = i;
        memcpy(frame + 1, scratch, SKY_FIGURE_SIZE);
        if(!b->send(b->send_ctx, BleMsgSave, frame, sizeof(frame)) && unsent) {
            unsent(unsent_ctx, i, scratch);
        }
    }
}

void sky_ble_flush_unsent(
    SkyBle* b,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx) {
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(!b->remote[i]) continue;
        lock(b);
        const bool dirty = sky_base_take_dirty(b->base, i, scratch);
        unlock(b);
        if(dirty && unsent) unsent(unsent_ctx, i, scratch);
    }
    memset(b->remote, 0, sizeof(b->remote));
}
