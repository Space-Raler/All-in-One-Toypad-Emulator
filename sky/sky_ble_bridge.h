/*
 * Skylanders BLE bridge: turns the BLE byte stream into portal actions.
 * Frame parsing is inf/ble_proto.c (shared with FlipBase, protocol version 2 here); the
 * message handling is sky_ble.c. This file ties the two to the BLE link and has no Flipper
 * dependencies, so it is tested on a PC (tests/sky_ble_bridge_test.c).
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../inf/ble_proto.h"
#include "core/sky_core.h"
#include "sky_ble.h"

typedef struct SkyBleLink SkyBleLink;

typedef struct {
    BleProtoParser parser;
    SkyBle ble;
    SkyBleLink* ble_link;
} SkyBleBridge;

SkyBleBridge* sky_ble_bridge_alloc(SkyBase* base); /* NULL if out of memory */
void sky_ble_bridge_free(SkyBleBridge* bridge);
void sky_ble_bridge_set_link(SkyBleBridge* bridge, SkyBleLink* link);

/* game = SkyGame value reported in STATUS. lock/unlock (optional) guard SkyBase access that is
 * shared with the USB interrupt. */
void sky_ble_bridge_configure(
    SkyBleBridge* bridge,
    uint8_t game,
    void (*lock)(void),
    void (*unlock)(void));
void sky_ble_bridge_set_usb_state(SkyBleBridge* bridge, uint8_t usb_state);
void sky_ble_bridge_set_file_ops(
    SkyBleBridge* bridge, void* file_ctx, SkyBleListDumpsFn list_dumps,
    SkyBleReadDumpFn read_dump, SkyBleWriteDumpFn write_dump);

void sky_ble_bridge_set_list_ops(
    SkyBleBridge* bridge, SkyBleListBeginFn begin, SkyBleListStepFn step, SkyBleListEndFn end);
/* Advance an active dump listing (call every app loop). */
void sky_ble_bridge_list_tick(SkyBleBridge* bridge);

/* Feed bytes received from the PC (any split). */
void sky_ble_bridge_pump(SkyBleBridge* bridge, const uint8_t* data, size_t size);
/* The stream went quiet mid-frame: rescan what is buffered. */
void sky_ble_bridge_resync(SkyBleBridge* bridge);
bool sky_ble_bridge_send(SkyBleBridge* bridge, uint8_t type, const uint8_t* payload, uint16_t len);

/* Send SAVE for PC-loaded figures the game changed; unsent data goes to `unsent` (may be NULL). */
void sky_ble_bridge_poll_saves(
    SkyBleBridge* bridge,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx);
/* Link is going away: hand changed PC-loaded figures to `unsent`, forget which came from the PC. */
void sky_ble_bridge_flush_unsent(
    SkyBleBridge* bridge,
    uint8_t scratch[SKY_FIGURE_SIZE],
    SkyBleSaveFn unsent,
    void* unsent_ctx);
bool sky_ble_bridge_slot_remote(const SkyBleBridge* bridge, uint8_t slot);
/* The app took this slot over (e.g. removed the figure by hand): stop treating it as the PC's. */
void sky_ble_bridge_forget_slot(SkyBleBridge* bridge, uint8_t slot);

/* Provided by sky_ble_link.c. */
bool sky_ble_link_send(SkyBleLink* link, uint8_t type, const uint8_t* payload, uint16_t len);
