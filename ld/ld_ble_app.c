/* LEGO Dimensions Bluetooth link, Flipper glue. See ld_ble_app.h. SPDX-License-Identifier: GPL-2.0-only */
#include "ld_ble_app.h"

#include <furi.h>
#include <stdlib.h>
#include <string.h>

#include "ToyPadEmu.h"
#include "ld_ble.h"
#include "minifigures.h"
#include "usb/usb_toypad.h"
#include "usb/save_toypad.h"
#include "../sky/sky_ble_link.h"

#define TOKEN_DELAY_MS 150 /* same pause the Flipper's own screen uses between pad changes */
#define STATUS_PUSH_MS 250

/* boxInfo is declared in views/EmulateToyPad_scene.h (via usb/save_toypad.h) */

static SkyBleLink* g_link = NULL;
static LdBle* g_ble = NULL;
static uint32_t g_rev = 0; /* bumped whenever the web page changes the pad */
static uint32_t g_last_push = 0;
static bool g_saved_changed = false; /* a vehicle was saved from the web page: refresh the Flipper's menu */
/* Tick at which the game last wrote to the vehicle in each box (0 = nothing waiting to be sent).
 * The game writes a vehicle a few bytes at a time, so we wait for it to go quiet first. */
static uint32_t g_dirty_at[NUM_BOXES];
#define VEHICLE_SETTLE_MS 400

static bool ld_send(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len) {
    UNUSED(ctx);
    return g_link && sky_ble_link_send(g_link, type, payload, len);
}

static uint8_t ops_usb_state(void* ctx) {
    UNUSED(ctx);
    const uint8_t s = get_connected_status();
    if(s == ConnectedStatusConnected) return LdUsbReady;
    /* The console has connected, but the Emulate screen resets the pad once when it first
     * draws, so tokens can only be placed after it has been opened. */
    if(s == ConnectedStatusReconnecting) return LdUsbOpenEmulate;
    return LdUsbNone;
}

static uint8_t ops_box_info(void* ctx, uint8_t box, uint16_t* id) {
    UNUSED(ctx);
    *id = 0;
    if(!emulator || box >= NUM_BOXES || !boxInfo[box].isFilled) return LdKindEmpty;
    const Token* t = ToyPadEmu_get_token(boxInfo[box].index);
    if(!t) return LdKindEmpty;
    if(is_vehicle(t)) {
        *id = (uint16_t)(t->token[0x24 * 4] | (t->token[0x24 * 4 + 1] << 8));
        return LdKindVehicle;
    }
    *id = (uint16_t)t->id;
    return LdKindMinifig;
}

static void flush_box(uint8_t box) {
    if(g_dirty_at[box] && g_ble) ld_ble_push_token(g_ble, box); /* last chance to send what the game wrote */
    g_dirty_at[box] = 0;
}

static bool remove_box(uint8_t box) {
    if(!boxInfo[box].isFilled) return false;
    flush_box(box);
    const int idx = boxInfo[box].index;
    const bool ok = idx >= 0 && ToyPadEmu_remove(idx);
    boxInfo[box].isFilled = false;
    boxInfo[box].index = -1;
    return ok;
}

static bool ops_get_token(void* ctx, uint8_t box, uint8_t uid[7], uint8_t data[180]) {
    UNUSED(ctx);
    if(!emulator || box >= NUM_BOXES || !boxInfo[box].isFilled) return false;
    const Token* t = ToyPadEmu_get_token(boxInfo[box].index);
    if(!t || !is_vehicle(t)) return false;
    memcpy(uid, t->uid, 7);
    memcpy(data, t->token, 180);
    return true;
}

static uint8_t ops_place_vehicle(void* ctx, uint8_t box, const uint8_t uid[7], const uint8_t data[180]) {
    UNUSED(ctx);
    if(!emulator || get_connected_status() != ConnectedStatusConnected) return BleResultNotReady;
    if(box >= NUM_BOXES) return BleResultBadSlot;
    const uint16_t id = (uint16_t)(data[0x24 * 4] | (data[0x24 * 4 + 1] << 8));
    const char* name = get_vehicle_name(id);
    if(strcmp(name, "?") == 0) return BleResultNotPresent; /* not a known vehicle id */

    if(boxInfo[box].isFilled) {
        remove_box(box);
        furi_delay_ms(TOKEN_DELAY_MS);
    }
    Token* token = malloc(sizeof(Token));
    if(!token) return BleResultUnknown;
    memset(token, 0, sizeof(Token)); /* id stays 0: that is what makes it a vehicle */
    memcpy(token->uid, uid, 7);
    memcpy(token->token, data, 180);
    snprintf(token->name, sizeof(token->name), "%s", name);
    if(!ToyPadEmu_place(token, box)) {
        free(token); /* no free token slot */
        return BleResultUnknown;
    }
    g_dirty_at[box] = 0;
    g_rev++;
    return BleResultOk;
}

static uint8_t ops_save_box(void* ctx, uint8_t box) {
    UNUSED(ctx);
    if(!emulator || box >= NUM_BOXES || !boxInfo[box].isFilled) return BleResultNotPresent;
    Token* t = ToyPadEmu_get_token(boxInfo[box].index);
    if(!t || !is_vehicle(t)) return BleResultNotPresent;
    if(!save_token(t)) return BleResultUnknown;
    g_saved_changed = true;
    return BleResultOk;
}

static void ops_saved_list(void* ctx, void (*emit)(void*, const char*), void* emit_ctx) {
    UNUSED(ctx);
    ld_saved_vehicle_list(emit, emit_ctx, LD_BLE_SAVED_MAX);
}

static bool ops_saved_read(void* ctx, const char* name, uint8_t uid[7], uint8_t data[180]) {
    UNUSED(ctx);
    return ld_saved_vehicle_read(name, uid, data);
}

static uint8_t ops_place(void* ctx, uint8_t box, uint8_t kind, uint16_t id, const uint32_t up[2]) {
    UNUSED(ctx);
    if(!emulator || get_connected_status() != ConnectedStatusConnected) return BleResultNotReady;
    if(box >= NUM_BOXES) return BleResultBadSlot;

    const bool vehicle = (kind == LdKindVehicle);
    const char* name = vehicle ? get_vehicle_name(id) : get_minifigure_name(id);
    if(strcmp(name, "?") == 0) return BleResultNotPresent; /* not a known character/vehicle id */

    if(boxInfo[box].isFilled) { /* replace: take the old one off first, like the Flipper's menu */
        remove_box(box);
        furi_delay_ms(TOKEN_DELAY_MS);
    }
    uint32_t upgrades[2] = {up[0], up[1]};
    Token* token = vehicle ? createVehicle(id, upgrades) : createCharacter(id);
    if(!token) return BleResultUnknown;
    if(!ToyPadEmu_place(token, box)) {
        free(token); /* no free token slot */
        return BleResultUnknown;
    }
    g_rev++;
    return BleResultOk;
}

static uint8_t ops_remove(void* ctx, uint8_t box) {
    UNUSED(ctx);
    if(!emulator || get_connected_status() != ConnectedStatusConnected) return BleResultNotReady;
    if(!boxInfo[box].isFilled) return BleResultNotPresent;
    remove_box(box);
    g_rev++;
    return BleResultOk;
}

static void ops_clear(void* ctx) {
    UNUSED(ctx);
    if(!emulator) return;
    for(uint8_t i = 0; i < NUM_BOXES; i++) flush_box(i);
    if(get_connected_status() == ConnectedStatusConnected) {
        ToyPadEmu_remove_all_tokens(); /* tells the console, one token at a time */
    } else {
        ToyPadEmu_clear(); /* nothing to tell: just forget them */
    }
    g_rev++;
}

static void feed_cb(void* ctx, const uint8_t* data, size_t size) {
    ld_ble_feed(ctx, data, size);
}

static void resync_cb(void* ctx) {
    ld_ble_resync(ctx);
}

bool ld_ble_app_start(void) {
    if(g_link) return true;
    static const LdBleOps ops = {
        .usb_state = ops_usb_state,
        .place = ops_place,
        .remove = ops_remove,
        .clear = ops_clear,
        .box_info = ops_box_info,
        .get_token = ops_get_token,
        .place_vehicle = ops_place_vehicle,
        .save_box = ops_save_box,
        .saved_list = ops_saved_list,
        .saved_read = ops_saved_read,
    };
    g_ble = malloc(sizeof(LdBle));
    if(!g_ble) return false;
    ld_ble_init(g_ble, &ops, NULL, ld_send, NULL);
    g_link = sky_ble_link_start();
    if(!g_link) {
        free(g_ble);
        g_ble = NULL;
        return false;
    }
    g_last_push = furi_get_tick();
    memset(g_dirty_at, 0, sizeof(g_dirty_at));
    __atomic_store_n(&ld_token_dirty_mask, 0, __ATOMIC_RELAXED);
    return true;
}

void ld_ble_app_stop(void) {
    SkyBleLink* link = g_link;
    g_link = NULL;
    if(link) sky_ble_link_stop(link);
    free(g_ble);
    g_ble = NULL;
}

bool ld_ble_app_enabled(void) {
    return g_link != NULL;
}

const char* ld_ble_app_status_text(void) {
    if(!g_link) return "Off";
    return sky_ble_link_connected(g_link) ? "PC connected" : "Waiting for PC";
}

bool ld_ble_app_take_saved_changed(void) {
    const bool r = g_saved_changed;
    g_saved_changed = false;
    return r;
}

bool ld_ble_app_tick(void) {
    if(!g_link || !g_ble) return false;
    const uint32_t rev = g_rev;
    sky_ble_link_pump_ex(g_link, feed_cb, resync_cb, g_ble);
    /* The game wrote to a vehicle (upgrades, levels): send it to the page once it goes quiet. */
    const uint32_t now = furi_get_tick();
    for(uint8_t box = 0; box < NUM_BOXES; box++) {
        const int idx = boxInfo[box].isFilled ? boxInfo[box].index : -1;
        if(idx >= 0 && idx < 8 &&
           (__atomic_load_n(&ld_token_dirty_mask, __ATOMIC_RELAXED) & (1u << idx))) {
            __atomic_fetch_and(&ld_token_dirty_mask, (uint8_t)~(1u << idx), __ATOMIC_RELAXED);
            g_dirty_at[box] = now ? now : 1;
        }
        if(g_dirty_at[box] && now - g_dirty_at[box] >= VEHICLE_SETTLE_MS) {
            if(!sky_ble_link_connected(g_link)) g_dirty_at[box] = 0; /* nobody to tell; LD_GET re-syncs later */
            else if(ld_ble_push_token(g_ble, box)) g_dirty_at[box] = 0;
            else g_dirty_at[box] = now; /* radio busy: try again shortly */
        }
    }
    /* The Flipper's own buttons (and the console) change the pad too: tell the page. */
    if(sky_ble_link_connected(g_link) && furi_get_tick() - g_last_push >= STATUS_PUSH_MS) {
        g_last_push = furi_get_tick();
        ld_ble_push_status_if_changed(g_ble);
    }
    return rev != g_rev;
}
