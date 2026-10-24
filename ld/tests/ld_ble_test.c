/* Host test for ld_ble.c. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../ld_ble.h"

static struct { uint8_t type; uint16_t len; uint8_t payload[256]; } sent[64];
static int n_sent;
static bool send_ok = true;
static bool fake_send(void* c, uint8_t type, const uint8_t* payload, uint16_t len) {
    (void)c;
    if(!send_ok) return false;
    sent[n_sent].type = type; sent[n_sent].len = len;
    if(len) memcpy(sent[n_sent].payload, payload, len);
    n_sent++;
    return true;
}

/* Fake toy pad */
static uint8_t usb = LdUsbReady;
static uint8_t kinds[LD_BLE_BOXES]; static uint16_t ids[LD_BLE_BOXES]; static uint32_t ups[2];
static uint8_t f_usb(void* c) { (void)c; return usb; }
static uint8_t f_place(void* c, uint8_t box, uint8_t kind, uint16_t id, const uint32_t up[2]) {
    (void)c;
    if(usb != LdUsbReady) return BleResultNotReady;
    if(id == 9999) return BleResultNotPresent; /* unknown character */
    kinds[box] = kind; ids[box] = id; ups[0] = up[0]; ups[1] = up[1];
    return BleResultOk;
}
static uint8_t f_remove(void* c, uint8_t box) { (void)c; if(!kinds[box]) return BleResultNotPresent; kinds[box] = 0; return BleResultOk; }
static void f_clear(void* c) { (void)c; memset(kinds, 0, sizeof kinds); }
static uint8_t f_info(void* c, uint8_t box, uint16_t* id) { (void)c; *id = ids[box]; return kinds[box]; }

/* Fake vehicle memory */
static uint8_t vuid[LD_BLE_BOXES][BLE_LD_UID_SIZE], vdata[LD_BLE_BOXES][BLE_LD_TOKEN_SIZE];
static bool f_get(void* c, uint8_t box, uint8_t uid[7], uint8_t data[180]) {
    (void)c; if(kinds[box] != LdKindVehicle) return false;
    memcpy(uid, vuid[box], 7); memcpy(data, vdata[box], 180); return true;
}
static uint8_t f_pveh(void* c, uint8_t box, const uint8_t uid[7], const uint8_t data[180]) {
    (void)c; if(data[0x24 * 4] == 0 && data[0x24 * 4 + 1] == 0) return BleResultNotPresent;
    kinds[box] = LdKindVehicle; ids[box] = (uint16_t)(data[0x24 * 4] | (data[0x24 * 4 + 1] << 8));
    memcpy(vuid[box], uid, 7); memcpy(vdata[box], data, 180); return BleResultOk;
}
static uint8_t f_save(void* c, uint8_t box) { (void)c; return kinds[box] == LdKindVehicle ? BleResultOk : BleResultNotPresent; }
static void f_list(void* c, void (*emit)(void*, const char*), void* ec) { (void)c; emit(ec, "Batmobile-0412.toy"); emit(ec, "TARDIS-0aa1.toy"); }
static bool f_read(void* c, const char* name, uint8_t uid[7], uint8_t data[180]) {
    (void)c; if(strcmp(name, "TARDIS-0aa1.toy")) return false;
    memset(uid, 0x44, 7); memset(data, 0x55, 180); return true;
}

static LdBle ble;
static void setup(void) {
    const LdBleOps ops = {f_usb, f_place, f_remove, f_clear, f_info, f_get, f_pveh, f_save, f_list, f_read};
    ld_ble_init(&ble, &ops, NULL, fake_send, NULL);
    n_sent = 0; send_ok = true; usb = LdUsbReady;
    memset(kinds, 0, sizeof kinds); memset(ids, 0, sizeof ids); memset(ups, 0, sizeof ups);
    memset(vuid, 0, sizeof vuid); memset(vdata, 0, sizeof vdata);
}
static void msg(uint8_t type, const uint8_t* p, uint16_t len) { ld_ble_on_message(&ble, type, p, len); }

int main(void) {
    /* HELLO -> STATUS v3, 25 bytes, nothing placed */
    setup(); msg(BleMsgHello, (const uint8_t[]){3}, 1);
    assert(n_sent == 1 && sent[0].type == BleMsgStatus && sent[0].len == 25);
    assert(sent[0].payload[0] == BLE_PROTO_VERSION_LD && sent[0].payload[1] == LdUsbReady);
    assert(sent[0].payload[2] == 0 && sent[0].payload[3] == 0);

    /* place Batman (1) on box 1, vehicle 1003 on box 5 */
    setup(); msg(BleMsgLdPlace, (const uint8_t[]){1, LdKindMinifig, 1, 0}, 4);
    assert(n_sent == 2 && sent[0].type == BleMsgResult && sent[0].payload[0] == BleMsgLdPlace &&
           sent[0].payload[1] == 1 && sent[0].payload[2] == BleResultOk && sent[1].type == BleMsgStatus);
    assert(sent[1].payload[2] == 0x02 && sent[1].payload[4 + 3 * 1] == LdKindMinifig && sent[1].payload[5 + 3] == 1);
    n_sent = 0;
    msg(BleMsgLdPlace, (const uint8_t[]){5, LdKindVehicle, 0xEB, 0x03, 1, 0, 0, 0, 2, 0, 0, 0}, 12);
    assert(kinds[5] == LdKindVehicle && ids[5] == 1003 && ups[0] == 1 && ups[1] == 2);
    assert(sent[1].payload[2] == 0x22 && sent[1].payload[4 + 3 * 5 + 1] == 0xEB && sent[1].payload[4 + 3 * 5 + 2] == 0x03);

    /* validation never reaches the pad */
    setup(); n_sent = 0;
    msg(BleMsgLdPlace, (const uint8_t[]){7, LdKindMinifig, 1, 0}, 4); assert(sent[0].payload[2] == BleResultBadSlot);
    msg(BleMsgLdPlace, (const uint8_t[]){0, 9, 1, 0}, 4);             assert(sent[1].payload[2] == BleResultUnknown);
    msg(BleMsgLdPlace, (const uint8_t[]){0, LdKindMinifig, 0, 0}, 4); assert(sent[2].payload[2] == BleResultUnknown);
    msg(BleMsgLdPlace, (const uint8_t[]){0, LdKindMinifig, 1}, 3);    assert(sent[3].payload[2] == BleResultBadLength);
    msg(BleMsgLdPlace, (const uint8_t[]){0, LdKindMinifig, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12); assert(sent[4].payload[2] == BleResultUnknown);
    for(int i = 0; i < LD_BLE_BOXES; i++) assert(!kinds[i]);

    /* pad refuses: reported, not placed; usb not ready reported */
    setup(); n_sent = 0;
    msg(BleMsgLdPlace, (const uint8_t[]){0, LdKindMinifig, 0x0F, 0x27}, 4); assert(sent[0].payload[2] == BleResultNotPresent && !kinds[0]);
    usb = LdUsbOpenEmulate; n_sent = 0;
    msg(BleMsgLdPlace, (const uint8_t[]){0, LdKindMinifig, 1, 0}, 4); assert(sent[0].payload[2] == BleResultNotReady);
    assert(sent[1].payload[1] == LdUsbOpenEmulate);

    /* remove + clear */
    setup(); msg(BleMsgLdPlace, (const uint8_t[]){2, LdKindMinifig, 3, 0}, 4); msg(BleMsgLdPlace, (const uint8_t[]){3, LdKindMinifig, 4, 0}, 4);
    n_sent = 0; msg(BleMsgRemove, (const uint8_t[]){2}, 1);
    assert(sent[0].payload[2] == BleResultOk && !kinds[2] && kinds[3]);
    msg(BleMsgRemove, (const uint8_t[]){2}, 1); assert(sent[2].payload[2] == BleResultNotPresent);
    msg(BleMsgRemove, (const uint8_t[]){9}, 1); assert(sent[4].payload[2] == BleResultBadSlot);
    msg(BleMsgClear, NULL, 0); assert(!kinds[3]);

    /* Flipper-side changes are pushed once, not repeatedly */
    setup(); msg(BleMsgStatusReq, NULL, 0); n_sent = 0;
    ld_ble_push_status_if_changed(&ble); assert(n_sent == 0);
    kinds[4] = LdKindMinifig; ids[4] = 7;
    ld_ble_push_status_if_changed(&ble); assert(n_sent == 1 && sent[0].payload[2] == 0x10);
    ld_ble_push_status_if_changed(&ble); assert(n_sent == 1);
    /* a failed send is retried on the next poll */
    kinds[4] = 0; send_ok = false; ld_ble_push_status_if_changed(&ble); send_ok = true;
    ld_ble_push_status_if_changed(&ble); assert(n_sent == 2 && sent[1].payload[2] == 0);

    /* end to end through the frame parser, split at odd places */
    setup(); uint8_t frame[64];
    const uint8_t body[4] = {2, LdKindMinifig, 5, 0};
    size_t n = ble_proto_encode(BleMsgLdPlace, body, 4, frame);
    ld_ble_feed(&ble, frame, 3); ld_ble_feed(&ble, frame + 3, n - 3);
    assert(kinds[2] == LdKindMinifig && ids[2] == 5);

    /* ---- vehicle memory ---- */
    {
        uint8_t pv[BLE_LD_TOKEN_FRAME]; memset(pv, 0, sizeof pv);
        pv[0] = 3; memset(pv + 1, 0x11, 7);
        uint8_t* d = pv + 8;
        d[0x23 * 4] = 0xAA; d[0x24 * 4] = 0xEB; d[0x24 * 4 + 1] = 0x03; d[0x25 * 4] = 0xBB;
        /* place saved vehicle keeps uid and every data byte */
        setup(); n_sent = 0; msg(BleMsgLdPlaceVehicle, pv, sizeof pv);
        assert(sent[0].type == BleMsgResult && sent[0].payload[2] == BleResultOk && sent[1].type == BleMsgStatus);
        assert(kinds[3] == LdKindVehicle && ids[3] == 1003 && vuid[3][0] == 0x11 && vdata[3][0x23 * 4] == 0xAA && vdata[3][0x25 * 4] == 0xBB);
        /* bad length / slot / no id */
        n_sent = 0; msg(BleMsgLdPlaceVehicle, pv, sizeof pv - 1); assert(sent[0].payload[2] == BleResultBadLength);
        pv[0] = 9; msg(BleMsgLdPlaceVehicle, pv, sizeof pv); assert(sent[1].payload[2] == BleResultBadSlot);
        pv[0] = 2; memset(pv + 8 + 0x24 * 4, 0, 2); msg(BleMsgLdPlaceVehicle, pv, sizeof pv); assert(sent[2].payload[2] == BleResultNotPresent && !kinds[2]);
        /* LD_GET returns LD_TOKEN with uid + data; a minifig / empty box is NotPresent */
        setup(); msg(BleMsgLdPlaceVehicle, (pv[0] = 3, pv[8 + 0x24 * 4] = 0xEB, pv[8 + 0x24 * 4 + 1] = 3, pv), sizeof pv);
        n_sent = 0; msg(BleMsgLdGet, (const uint8_t[]){3}, 1);
        assert(n_sent == 1 && sent[0].type == BleMsgLdToken && sent[0].len == BLE_LD_TOKEN_FRAME && sent[0].payload[0] == 3 && sent[0].payload[1] == 0x11);
        assert(sent[0].payload[8 + 0x24 * 4] == 0xEB);
        msg(BleMsgLdGet, (const uint8_t[]){4}, 1); assert(sent[1].type == BleMsgResult && sent[1].payload[2] == BleResultNotPresent);
        msg(BleMsgLdGet, (const uint8_t[]){8}, 1); assert(sent[2].payload[2] == BleResultBadSlot);
        /* push_token: the game changed it */
        n_sent = 0; assert(ld_ble_push_token(&ble, 3) && n_sent == 1 && sent[0].type == BleMsgLdToken);
        send_ok = false; assert(!ld_ble_push_token(&ble, 3)); send_ok = true; assert(!ld_ble_push_token(&ble, 5));
        /* save to the Flipper */
        n_sent = 0; msg(BleMsgLdSaveVehicle, (const uint8_t[]){3}, 1); assert(sent[0].payload[2] == BleResultOk);
        msg(BleMsgLdSaveVehicle, (const uint8_t[]){4}, 1); assert(sent[1].payload[2] == BleResultNotPresent);
        /* saved list ends with an empty frame */
        n_sent = 0; msg(BleMsgLdSavedList, NULL, 0);
        assert(n_sent == 3 && sent[0].type == BleMsgLdSavedItem && sent[0].len == 18 && sent[1].len == 15 && sent[2].type == BleMsgLdSavedItem && sent[2].len == 0);
        /* saved read: ok, missing, and path tricks are refused before touching the card */
        n_sent = 0; msg(BleMsgLdSavedRead, (const uint8_t*)"TARDIS-0aa1.toy", 15);
        assert(n_sent == 1 && sent[0].type == BleMsgLdToken && sent[0].payload[0] == BLE_LD_BOX_FILE && sent[0].payload[1] == 0x44 && sent[0].payload[8] == 0x55);
        msg(BleMsgLdSavedRead, (const uint8_t*)"Nope-0000.toy", 13); assert(sent[1].payload[2] == BleResultNotPresent);
        msg(BleMsgLdSavedRead, (const uint8_t*)"../x/y.toy", 10); assert(sent[2].payload[2] == BleResultBadLength);
        msg(BleMsgLdSavedRead, (const uint8_t*)"a\\b.toy", 6); assert(sent[3].payload[2] == BleResultBadLength);
        msg(BleMsgLdSavedRead, (const uint8_t*)".hid.toy", 8); assert(sent[4].payload[2] == BleResultBadLength);
        msg(BleMsgLdSavedRead, (const uint8_t*)"TARDIS-0aa1.bin", 15); assert(sent[5].payload[2] == BleResultBadLength);
        /* old Flipper ops (no vehicle memory) answer "unknown", never crash */
        LdBleOps old; memset(&old, 0, sizeof old); old.usb_state = f_usb; old.place = f_place; old.remove = f_remove; old.clear = f_clear; old.box_info = f_info;
        ld_ble_init(&ble, &old, NULL, fake_send, NULL); n_sent = 0;
        msg(BleMsgLdPlaceVehicle, pv, sizeof pv); assert(sent[0].payload[2] == BleResultUnknown);
        msg(BleMsgLdSaveVehicle, (const uint8_t[]){3}, 1); assert(sent[1].payload[2] == BleResultUnknown);
        msg(BleMsgLdSavedList, NULL, 0); assert(sent[2].type == BleMsgLdSavedItem && sent[2].len == 0);
        msg(BleMsgLdSavedRead, (const uint8_t*)"TARDIS-0aa1.toy", 15); assert(sent[3].payload[2] == BleResultNotPresent);
        assert(!ld_ble_push_token(&ble, 3));
    }

    puts("ld_ble_test: ok");
    return 0;
}
