/*
 * Skylanders BLE link, message handling (platform independent; builds on the Flipper and on a PC).
 * The PC "portal" app sends 1024-byte figure dumps; this loads them into the SkyBase portal core
 * and reports back. Frames come from ble_proto.c (protocol version 2, see ble_proto.h).
 *
 * PC -> Flipper: HELLO, LOAD [slot][1024], REMOVE [slot], CLEAR, STATUS_REQ
 * Flipper -> PC: STATUS [version=2][usb_state][present_lo][present_hi][game],
 *                RESULT [request][slot][code], SAVE [slot][1024] (the game changed this figure)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/sky_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*SkyBleSendFn)(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len);
/* Persist data that could not be sent to the PC (the app writes unsent_slotN.bin). */
typedef void (*SkyBleSaveFn)(void* ctx, uint8_t slot, const uint8_t data[SKY_FIGURE_SIZE]);

typedef struct {
    SkyBase* base;
    SkyBleSendFn send;
    void* send_ctx;
    void (*lock)(void); /* optional: guards SkyBase access shared with the USB interrupt */
    void (*unlock)(void);
    uint8_t game; /* SkyGame value reported in STATUS */
    uint8_t usb_state; /* UsbSkyState value reported in STATUS */
    bool remote[SKY_SLOT_COUNT]; /* figure in this slot came from the PC */
} SkyBle;

void sky_ble_init(SkyBle* b, SkyBase* base, SkyBleSendFn send, void* send_ctx);

/* Matches BleProtoHandler; ctx is the SkyBle*. */
void sky_ble_on_message(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len);

/* Send SAVE for each PC-loaded slot whose data the game changed; if a send fails, hand the data
 * to `unsent` instead so nothing is lost. scratch is SKY_FIGURE_SIZE bytes. */
void sky_ble_poll_saves(
    SkyBle* b,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx);

/* BLE is being switched off: hand every changed PC-loaded slot to `unsent`, then forget that any
 * slot came from the PC (the figures stay on the portal). */
void sky_ble_flush_unsent(
    SkyBle* b,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx);

#ifdef __cplusplus
}
#endif
