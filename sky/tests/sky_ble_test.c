/* Host test for sky_ble.c (Skylanders BLE message handler), against the real SkyBase core. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ble_proto.h"
#include "sky_ble.h"
#include "sky_core.h"

#define MAX_SENT 32
static struct {
    uint8_t type;
    uint16_t len;
    uint8_t payload[1 + SKY_FIGURE_SIZE];
} sent[MAX_SENT];
static int n_sent;
static bool send_ok = true;

static bool fake_send(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len) {
    (void)ctx;
    if(!send_ok) return false;
    assert(n_sent < MAX_SENT);
    sent[n_sent].type = type;
    sent[n_sent].len = len;
    memcpy(sent[n_sent].payload, payload, len);
    n_sent++;
    return true;
}

static int unsent_calls;
static uint8_t unsent_slot;
static uint8_t unsent_data[SKY_FIGURE_SIZE];
static void fake_unsent(void* ctx, uint8_t slot, const uint8_t data[SKY_FIGURE_SIZE]) {
    (void)ctx;
    unsent_calls++;
    unsent_slot = slot;
    memcpy(unsent_data, data, SKY_FIGURE_SIZE);
}

static SkyBase base;
static SkyBle ble;
static uint8_t fig_a[1 + SKY_FIGURE_SIZE];
static uint8_t fig_b[1 + SKY_FIGURE_SIZE];

static void reset(void) {
    sky_base_init(&base);
    sky_ble_init(&ble, &base, fake_send, NULL);
    ble.game = 3;
    ble.usb_state = 2;
    n_sent = 0;
    send_ok = true;
    unsent_calls = 0;
    memset(fig_a, 0, sizeof(fig_a));
    memset(fig_b, 0, sizeof(fig_b));
    for(int i = 1; i <= SKY_FIGURE_SIZE; i++) {
        fig_a[i] = (uint8_t)(i * 7);
        fig_b[i] = (uint8_t)(i * 13 + 1);
    }
}

static void msg(uint8_t type, const uint8_t* p, uint16_t len) {
    sky_ble_on_message(&ble, type, p, len);
}

static void load(uint8_t slot, uint8_t* fig) {
    fig[0] = slot;
    msg(BleMsgLoad, fig, 1 + SKY_FIGURE_SIZE);
}

static void expect_result(int idx, uint8_t request, uint8_t slot, uint8_t code) {
    assert(idx < n_sent);
    assert(sent[idx].type == BleMsgResult && sent[idx].len == 3);
    assert(sent[idx].payload[0] == request);
    assert(sent[idx].payload[1] == slot);
    assert(sent[idx].payload[2] == code);
}

static void test_hello_status_v2(void) {
    reset();
    ble.base->slots[0].status = 1;
    ble.base->slots[2].status = 1;
    uint8_t v = 2;
    msg(BleMsgHello, &v, 1);
    assert(n_sent == 1 && sent[0].type == BleMsgStatus && sent[0].len == 5);
    assert(sent[0].payload[0] == 2 && sent[0].payload[1] == 2);
    assert(sent[0].payload[2] == 0x05 && sent[0].payload[3] == 0 && sent[0].payload[4] == 3);
}

static void test_load_ok_marks_remote_and_present(void) {
    reset();
    load(3, fig_a);
    expect_result(0, BleMsgLoad, 3, BleResultOk);
    assert(sky_base_slot_present(&base, 3) && ble.remote[3] && !ble.remote[2]);
    assert(memcmp(base.slots[3].data, fig_a + 1, SKY_FIGURE_SIZE) == 0);
}

static void test_load_bad_length_320(void) {
    reset();
    uint8_t small[1 + 320] = {1};
    msg(BleMsgLoad, small, sizeof(small));
    expect_result(0, BleMsgLoad, 1, BleResultBadLength);
    assert(!sky_base_slot_present(&base, 1));
}

static void test_load_bad_slot_8(void) {
    reset();
    load(8, fig_a);
    expect_result(0, BleMsgLoad, 8, BleResultBadSlot);
}

static void test_load_occupied_replaces(void) {
    reset();
    load(4, fig_a);
    n_sent = 0;
    load(4, fig_b);
    expect_result(0, BleMsgLoad, 4, BleResultOk);
    assert(sky_base_slot_present(&base, 4) && ble.remote[4]);
    assert(memcmp(base.slots[4].data, fig_b + 1, SKY_FIGURE_SIZE) == 0);
}

static void test_remove_ok_and_not_present(void) {
    reset();
    load(1, fig_a);
    n_sent = 0;
    uint8_t s = 1;
    msg(BleMsgRemove, &s, 1);
    expect_result(0, BleMsgRemove, 1, BleResultOk);
    assert(!sky_base_slot_present(&base, 1) && !ble.remote[1]);
    n_sent = 0;
    msg(BleMsgRemove, &s, 1);
    expect_result(0, BleMsgRemove, 1, BleResultNotPresent);
}

static void test_clear_removes_all_and_clears_remote(void) {
    reset();
    load(0, fig_a);
    load(7, fig_b);
    base.slots[5].status = 1; /* a figure loaded from the SD card */
    n_sent = 0;
    msg(BleMsgClear, NULL, 0);
    expect_result(0, BleMsgClear, 0, BleResultOk);
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) assert(!sky_base_slot_present(&base, i) && !ble.remote[i]);
}

static void test_poll_saves_sends_save(void) {
    reset();
    load(2, fig_a);
    n_sent = 0;
    sky_base_mark_dirty(&base, 2);
    uint8_t scratch[SKY_FIGURE_SIZE];
    sky_ble_poll_saves(&ble, scratch, fake_unsent, NULL);
    assert(n_sent == 1 && sent[0].type == BleMsgSave && sent[0].len == 1 + SKY_FIGURE_SIZE);
    assert(sent[0].payload[0] == 2);
    assert(memcmp(sent[0].payload + 1, fig_a + 1, SKY_FIGURE_SIZE) == 0);
    assert(unsent_calls == 0 && !base.slots[2].dirty);
}

static void test_poll_saves_unsent_when_send_fails(void) {
    reset();
    load(6, fig_b);
    sky_base_mark_dirty(&base, 6);
    send_ok = false;
    uint8_t scratch[SKY_FIGURE_SIZE];
    for(int k = 0; k < SKY_BLE_SAVE_RETRIES - 1; k++) { /* retried first, nothing parked yet */
        sky_ble_poll_saves(&ble, scratch, fake_unsent, NULL);
        assert(unsent_calls == 0 && base.slots[6].dirty);
    }
    sky_ble_poll_saves(&ble, scratch, fake_unsent, NULL); /* last try fails: parked */
    assert(unsent_calls == 1 && unsent_slot == 6 && ble.saves_failed == SKY_BLE_SAVE_RETRIES);
    assert(memcmp(unsent_data, fig_b + 1, SKY_FIGURE_SIZE) == 0);
    send_ok = true; /* a later success after one failure goes through and resets the counter */
    sky_base_mark_dirty(&base, 6);
    sky_ble_poll_saves(&ble, scratch, fake_unsent, NULL);
    assert(ble.saves_sent == 1 && ble.fails[6] == 0);
}

static void test_flush_unsent_writes_dirty_and_clears_remote(void) {
    reset();
    load(1, fig_a);
    load(2, fig_b);
    sky_base_mark_dirty(&base, 2);
    uint8_t scratch[SKY_FIGURE_SIZE];
    n_sent = 0;
    sky_ble_flush_unsent(&ble, scratch, fake_unsent, NULL);
    assert(unsent_calls == 1 && unsent_slot == 2 && n_sent == 0);
    assert(!ble.remote[1] && !ble.remote[2]);
    assert(sky_base_slot_present(&base, 1)); /* the console keeps seeing it */
}

static void test_non_remote_dirty_slot_ignored(void) {
    reset();
    base.slots[3].status = 1; /* from the SD card, not the PC */
    sky_base_mark_dirty(&base, 3);
    uint8_t scratch[SKY_FIGURE_SIZE];
    sky_ble_poll_saves(&ble, scratch, fake_unsent, NULL);
    assert(n_sent == 0 && unsent_calls == 0 && base.slots[3].dirty);
}

int main(void) {
    test_hello_status_v2();
    test_load_ok_marks_remote_and_present();
    test_load_bad_length_320();
    test_load_bad_slot_8();
    test_load_occupied_replaces();
    test_remove_ok_and_not_present();
    test_clear_removes_all_and_clears_remote();
    test_poll_saves_sends_save();
    test_poll_saves_unsent_when_send_fails();
    test_flush_unsent_writes_dirty_and_clears_remote();
    test_non_remote_dirty_slot_ignored();
    puts("sky_ble_test: all passed");
    return 0;
}
