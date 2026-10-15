/*
 * FlipBase BLE link: wire protocol between the PC "base" app and the Flipper.
 * Platform independent (builds on the Flipper and on a PC) so it can be tested.
 *
 * Frame:  0xA5 | type | len_lo | len_hi | payload[len] | crc8
 * crc8:   polynomial 0x07, init 0, over type, len_lo, len_hi and the payload.
 * Frames ride on a BLE serial stream, so they may arrive split or joined at any
 * byte; the parser is a byte-wise state machine.
 *
 * PC -> Flipper                              payload
 *   0x01 HELLO       [version]
 *   0x02 LOAD        [slot][320 bytes figure image]
 *   0x03 REMOVE      [slot]
 *   0x04 CLEAR       (empty)                 take every figure off the base
 *   0x05 STATUS_REQ  (empty)
 * Flipper -> PC
 *   0x81 STATUS      [version][usb_state][present_lo][present_hi]
 *   0x82 RESULT      [request type][slot][code]
 *   0x83 SAVE        [slot][320 bytes]       the game changed this figure
 *
 * Slots are the base's own slot numbers 0..8 (same numbers as infinity_core.h).
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_PROTO_VERSION 1 /* Infinity (FlipBase): 320-byte figures */
#define BLE_PROTO_VERSION_SKY 2 /* Skylanders: 1024-byte figures, STATUS carries the game */
#define BLE_PROTO_SOF     0xA5
#define BLE_PROTO_FIGURE  320
#define BLE_PROTO_SKY_FIGURE 1024
#define BLE_PROTO_MAX_PAYLOAD (1 + BLE_PROTO_SKY_FIGURE)
#define BLE_PROTO_MAX_FRAME   (5 + BLE_PROTO_MAX_PAYLOAD)

enum {
    BleMsgHello = 0x01,
    BleMsgLoad = 0x02,
    BleMsgRemove = 0x03,
    BleMsgClear = 0x04,
    BleMsgStatusReq = 0x05,
    BleMsgStatus = 0x81,
    BleMsgResult = 0x82,
    BleMsgSave = 0x83,
};

/* RESULT codes. */
enum {
    BleResultOk = 0,
    BleResultBadSlot = 1,
    BleResultBadLength = 2,
    BleResultNotPresent = 3,
    BleResultUnknown = 4,
};

typedef void (*BleProtoHandler)(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len);

typedef struct {
    uint16_t n; /* bytes buffered */
    uint32_t bad_frames;
    uint8_t buf[BLE_PROTO_MAX_FRAME];
} BleProtoParser;

void ble_proto_init(BleProtoParser* p);

/* The stream went quiet with a partial frame buffered: give up on its start marker and
 * rescan what is left (frames hiding behind line noise are still delivered). */
void ble_proto_resync(BleProtoParser* p, BleProtoHandler handler, void* ctx);

/* True while bytes are buffered waiting for the rest of a frame. */
int ble_proto_busy(const BleProtoParser* p);

/* Feed received bytes; handler is called once per good frame. */
void ble_proto_feed(
    BleProtoParser* p,
    const uint8_t* data,
    size_t size,
    BleProtoHandler handler,
    void* ctx);

/* Build a frame into out (needs 5 + len bytes). Returns the frame size. */
size_t ble_proto_encode(uint8_t type, const uint8_t* payload, uint16_t len, uint8_t* out);

#ifdef __cplusplus
}
#endif
