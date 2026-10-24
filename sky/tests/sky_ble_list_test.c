/* Host test for the incremental, batched dump listing in sky_ble.c. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_proto.h"
#include "sky_ble.h"
#include "sky_core.h"

#define MAX_FRAMES 512
static struct { uint8_t type; uint16_t len; uint8_t payload[BLE_PROTO_MAX_PAYLOAD]; } sent[MAX_FRAMES];
static int n_sent;
static int fail_after = -1; /* number of successful sends before every send fails; -1 = never */

static bool fake_send(void* ctx, uint8_t type, const uint8_t* payload, uint16_t len) {
    (void)ctx;
    if(fail_after >= 0 && n_sent >= fail_after) return false;
    assert(n_sent < MAX_FRAMES);
    sent[n_sent].type = type;
    sent[n_sent].len = len;
    if(len) memcpy(sent[n_sent].payload, payload, len);
    n_sent++;
    return true;
}

/* Fake SD card: a list of paths, one "directory entry" per step. */
#define MAX_PATHS 2000
static char paths[MAX_PATHS][256];
static int n_paths, cursor, begins, ends, steps;
static void f_begin(void* c) { (void)c; begins++; cursor = 0; }
static bool f_step(void* c, SkyBleEmitPathFn emit, void* ctx) {
    (void)c;
    steps++;
    if(cursor >= n_paths) return false;
    emit(ctx, paths[cursor++]);
    return cursor < n_paths;
}
static void f_end(void* c) { (void)c; ends++; }

static SkyBase base;
static SkyBle ble;

static void setup(void) {
    sky_base_init(&base);
    sky_ble_init(&ble, &base, fake_send, NULL);
    sky_ble_set_list_ops(&ble, f_begin, f_step, f_end);
    n_sent = 0; begins = ends = steps = 0; fail_after = -1;
}

static void fill(int games, int per_game) {
    n_paths = 0;
    for(int g = 0; g < games; g++)
        for(int i = 0; i < per_game; i++)
            snprintf(paths[n_paths++], 256,
                "/ext/apps_data/skybase/giants/2. Giants/%d) Category %d/Figure Number %d.dump", g, g, i);
}

/* Decode the received frames the way index.html does. */
static int decode(char out[][256], int max) {
    char dir[256] = ""; (void)0;
    int n = 0, terminated = 0;
    for(int f = 0; f < n_sent; f++) {
        if(sent[f].type != BleMsgDumpListItem) continue;
        if(!sent[f].len) { terminated++; continue; }
        char tmp[BLE_PROTO_MAX_PAYLOAD + 1];
        memcpy(tmp, sent[f].payload, sent[f].len);
        tmp[sent[f].len] = 0;
        for(char* line = strtok(tmp, "\n"); line; line = strtok(NULL, "\n")) {
            if(line[0] == '>') { snprintf(dir, sizeof(dir), "%s", line + 1); continue; }
            assert(n < max);
            if(line[0] == '/') snprintf(out[n++], 256, "%s", line);
            else { int l = snprintf(out[n], 256, "%s/%s", dir, line); assert(l > 0 && l < 256); n++; }
        }
    }
    assert(terminated == 1);
    return n;
}

static void run_to_end(void) {
    for(int i = 0; i < 100000 && ble.list_active; i++) sky_ble_list_tick(&ble);
    assert(!ble.list_active);
}

static void start(void) {
    uint8_t frame[8];
    (void)frame;
    sky_ble_on_message(&ble, BleMsgDumpList, NULL, 0);
}

int main(void) {
    static char got[MAX_PATHS][256];

    /* 1. Everything arrives, in order, exactly once, with one terminator. */
    setup(); fill(6, 100); start();
    assert(ble.list_active && n_sent == 0); /* handler itself does no work */
    run_to_end();
    assert(decode(got, MAX_PATHS) == n_paths);
    for(int i = 0; i < n_paths; i++) assert(strcmp(got[i], paths[i]) == 0);
    assert(begins == 1 && ends == 1);
    /* Batched: far fewer frames than files. */
    printf("600 files -> %d frames, %d payload bytes\n", n_sent, ({ int t = 0; for(int i = 0; i < n_sent; i++) t += sent[i].len; t; }));
    assert(n_sent < 80);

    /* 2. Empty card: just the terminator. */
    setup(); n_paths = 0; start(); run_to_end();
    assert(n_sent == 1 && sent[0].type == BleMsgDumpListItem && sent[0].len == 0);

    /* 3. Radio dies mid-listing: stop right away (no per-file retry storm). */
    setup(); fill(6, 100); fail_after = 3; start(); run_to_end();
    assert(ends == 1 && steps < n_paths); /* gave up early, lister was released */

    /* 4. A second request restarts the listing and releases the first lister. */
    setup(); fill(2, 50); start(); sky_ble_list_tick(&ble); start();
    assert(begins == 2 && ends == 1); run_to_end();
    assert(ends == 2);

    /* 5. Cancel releases resources. */
    setup(); fill(2, 50); start(); sky_ble_list_cancel(&ble);
    assert(ends == 1 && !ble.list_active);

    /* 6. A tick sends at most one frame. */
    setup(); fill(6, 100); start();
    for(int i = 0; i < 20; i++) { const int before = n_sent; sky_ble_list_tick(&ble); assert(n_sent - before <= 1); }

    puts("sky_ble_list_test: ok");
    return 0;
}
