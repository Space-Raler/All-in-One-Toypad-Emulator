/* Host test for sky_ble_bridge.c: the byte-stream side (parser + handler + SAVE) of the
 * Skylanders BLE link, with sky_ble_link_send stubbed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sky_ble_bridge.h"

#define MAX_SENT 32
static struct {
    uint8_t type;
    uint16_t len;
    uint8_t payload[1 + SKY_FIGURE_SIZE];
} sent[MAX_SENT];
static int n_sent;
static bool link_ok = true;

bool sky_ble_link_send(SkyBleLink* link, uint8_t type, const uint8_t* payload, uint16_t len) {
    (void)link;
    if(!link_ok) return false;
    assert(n_sent < MAX_SENT);
    sent[n_sent].type = type;
    sent[n_sent].len = len;
    memcpy(sent[n_sent].payload, payload, len);
    n_sent++;
    return true;
}

static SkyBase base;
static SkyBleBridge* bridge;
static uint8_t figure[SKY_FIGURE_SIZE];
static int locks, unlocks;
static void lock(void) { locks++; }
static void unlock(void) { unlocks++; }

static void reset(void) {
    if(bridge) sky_ble_bridge_free(bridge);
    sky_base_init(&base);
    bridge = sky_ble_bridge_alloc(&base);
    assert(bridge);
    sky_ble_bridge_set_link(bridge, (SkyBleLink*)&base); /* any non-NULL pointer; the stub ignores it */
    sky_ble_bridge_configure(bridge, 4, lock, unlock);
    sky_ble_bridge_set_usb_state(bridge, 3);
    n_sent = 0;
    link_ok = true;
    locks = unlocks = 0;
    for(int i = 0; i < SKY_FIGURE_SIZE; i++) figure[i] = (uint8_t)(i * 5 + 3);
}

static size_t frame(uint8_t type, const uint8_t* p, uint16_t len, uint8_t* out) {
    return ble_proto_encode(type, p, len, out);
}

/* Feed a byte stream in awkward chunk sizes, as BLE does. */
static void feed(const uint8_t* data, size_t n) {
    static const size_t chunks[] = {1, 7, 20, 3, 64, 5, 200, 513};
    size_t pos = 0, c = 0;
    while(pos < n) {
        size_t take = chunks[c++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if(take > n - pos) take = n - pos;
        sky_ble_bridge_pump(bridge, data + pos, take);
        pos += take;
    }
}

static void test_load_1024_in_odd_chunks(void) {
    reset();
    uint8_t payload[1 + SKY_FIGURE_SIZE];
    payload[0] = 5;
    memcpy(payload + 1, figure, SKY_FIGURE_SIZE);
    uint8_t buf[5 + sizeof(payload)];
    feed(buf, frame(BleMsgLoad, payload, sizeof(payload), buf));
    assert(sky_base_slot_present(&base, 5));
    assert(memcmp(base.slots[5].data, figure, SKY_FIGURE_SIZE) == 0);
    assert(sky_ble_bridge_slot_remote(bridge, 5));
    assert(n_sent >= 1 && sent[0].type == BleMsgResult);
    assert(sent[0].payload[0] == BleMsgLoad && sent[0].payload[1] == 5 && sent[0].payload[2] == BleResultOk);
    assert(locks > 0 && locks == unlocks);
}

static void test_infinity_320_byte_load_gets_badlength(void) {
    reset();
    uint8_t payload[1 + 320] = {2};
    uint8_t buf[5 + sizeof(payload)];
    feed(buf, frame(BleMsgLoad, payload, sizeof(payload), buf));
    assert(!sky_base_slot_present(&base, 2));
    assert(n_sent == 1 && sent[0].type == BleMsgResult && sent[0].payload[2] == BleResultBadLength);
}

static void test_hello_reports_game_and_usb_state(void) {
    reset();
    uint8_t v = 2, buf[16];
    feed(buf, frame(BleMsgHello, &v, 1, buf));
    assert(n_sent == 1 && sent[0].type == BleMsgStatus && sent[0].len == 5);
    assert(sent[0].payload[1] == 3 && sent[0].payload[4] == 4);
}

static void test_resync_delivers_frame_hidden_behind_noise(void) {
    reset();
    uint8_t v = 2, hello[16], stream[64];
    const size_t hn = frame(BleMsgHello, &v, 1, hello);
    const uint8_t junk[] = {0xA5, 0x02, 0xFF, 0x03, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}; /* claims 1023 bytes */
    memcpy(stream, junk, sizeof(junk));
    memcpy(stream + sizeof(junk), hello, hn);
    sky_ble_bridge_pump(bridge, stream, sizeof(junk) + hn);
    assert(n_sent == 0); /* swallowed while waiting for the rest of the bogus frame */
    sky_ble_bridge_resync(bridge);
    assert(n_sent == 1 && sent[0].type == BleMsgStatus);
}

static void test_save_roundtrip_and_unsent(void) {
    reset();
    uint8_t payload[1 + SKY_FIGURE_SIZE];
    payload[0] = 1;
    memcpy(payload + 1, figure, SKY_FIGURE_SIZE);
    uint8_t buf[5 + sizeof(payload)];
    feed(buf, frame(BleMsgLoad, payload, sizeof(payload), buf));
    n_sent = 0;
    sky_base_mark_dirty(&base, 1);
    uint8_t scratch[SKY_FIGURE_SIZE];
    sky_ble_bridge_poll_saves(bridge, scratch, NULL, NULL);
    assert(n_sent == 1 && sent[0].type == BleMsgSave && sent[0].payload[0] == 1);
    assert(memcmp(sent[0].payload + 1, figure, SKY_FIGURE_SIZE) == 0);
    link_ok = false; /* PC gone */
    sky_base_mark_dirty(&base, 1);
    sky_ble_bridge_flush_unsent(bridge, scratch, NULL, NULL); /* NULL callback must be safe */
    assert(!sky_ble_bridge_slot_remote(bridge, 1));
}

static void test_forget_slot_stops_saves_for_that_slot_only(void) {
    reset();
    uint8_t payload[1 + SKY_FIGURE_SIZE], buf[5 + sizeof(payload)];
    memcpy(payload + 1, figure, SKY_FIGURE_SIZE);
    payload[0] = 0;
    feed(buf, frame(BleMsgLoad, payload, sizeof(payload), buf));
    payload[0] = 4;
    feed(buf, frame(BleMsgLoad, payload, sizeof(payload), buf));
    n_sent = 0;
    sky_ble_bridge_forget_slot(bridge, 4);
    assert(sky_ble_bridge_slot_remote(bridge, 0) && !sky_ble_bridge_slot_remote(bridge, 4));
    sky_base_mark_dirty(&base, 4);
    uint8_t scratch[SKY_FIGURE_SIZE];
    sky_ble_bridge_poll_saves(bridge, scratch, NULL, NULL);
    assert(n_sent == 0);
}

int main(void) {
    test_load_1024_in_odd_chunks();
    test_infinity_320_byte_load_gets_badlength();
    test_hello_reports_game_and_usb_state();
    test_resync_delivers_frame_hidden_behind_noise();
    test_save_roundtrip_and_unsent();
    test_forget_slot_stops_saves_for_that_slot_only();
    puts("sky_ble_bridge_test: all passed");
    return 0;
}
