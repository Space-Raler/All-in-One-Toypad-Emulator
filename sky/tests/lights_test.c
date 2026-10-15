/* Host test for the light commands in sky_core ('C', 'L', 'J').
 * gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Icore \
 *     core/sky_core.c tests/lights_test.c -o /tmp/lt && /tmp/lt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sky_core.h"

#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            exit(1); \
        } \
    } while(0)

static void cmd(SkyBase* b, const uint8_t* d, size_t n) {
    sky_base_handle_command(b, d, n);
}

int main(void) {
    SkyBase* b = malloc(sizeof(SkyBase));
    CHECK(b);
    sky_base_init(b);
    CHECK(b->lights.seq == 0 && !b->lights.backlight_set && b->lights.fade_ms == 0);

    /* C: ring colour, immediate, no reply */
    const uint8_t c[] = {'C', 10, 20, 30};
    cmd(b, c, sizeof(c));
    CHECK(b->lights.ring[0] == 10 && b->lights.ring[1] == 20 && b->lights.ring[2] == 30);
    CHECK(b->lights.fade_ms == 0 && b->lights.seq == 1);
    CHECK(b->r_count == 0);

    /* C too short is ignored */
    const uint8_t c_short[] = {'C', 1, 2};
    cmd(b, c_short, sizeof(c_short));
    CHECK(b->lights.seq == 1 && b->lights.ring[0] == 10);

    /* A full 32-byte report with trailing zeros still reads the first bytes */
    uint8_t c32[32] = {'C', 255, 0, 128};
    cmd(b, c32, sizeof(c32));
    CHECK(b->lights.ring[0] == 255 && b->lights.ring[1] == 0 && b->lights.ring[2] == 128);
    CHECK(b->lights.seq == 2);

    /* L side 0: ring colour; side 1: backlight level; side 3: full; others ignored */
    const uint8_t l0[] = {'L', 0, 1, 2, 3};
    cmd(b, l0, sizeof(l0));
    CHECK(b->lights.ring[0] == 1 && b->lights.ring[1] == 2 && b->lights.ring[2] == 3);
    CHECK(b->lights.seq == 3 && b->r_count == 0);

    const uint8_t l1[] = {'L', 1, 40};
    cmd(b, l1, sizeof(l1));
    CHECK(b->lights.backlight_set && b->lights.backlight == 40 && b->lights.seq == 4);

    const uint8_t l1zero[] = {'L', 1, 0};
    cmd(b, l1zero, sizeof(l1zero));
    CHECK(b->lights.backlight == 0 && b->lights.seq == 5);

    const uint8_t l3[] = {'L', 3, 0};
    cmd(b, l3, sizeof(l3));
    CHECK(b->lights.backlight == 255 && b->lights.seq == 6);

    const uint8_t l2[] = {'L', 2, 9, 9, 9};
    cmd(b, l2, sizeof(l2));
    CHECK(b->lights.seq == 6 && b->lights.ring[0] == 1); /* side 2 does nothing */

    const uint8_t l0_short[] = {'L', 0, 1};
    cmd(b, l0_short, sizeof(l0_short));
    CHECK(b->lights.seq == 6); /* side 0 needs r,g,b */

    const uint8_t l_tiny[] = {'L', 1};
    cmd(b, l_tiny, sizeof(l_tiny));
    CHECK(b->lights.seq == 6);

    /* J: fade, still acked with a reply */
    const uint8_t j[] = {'J', 0, 100, 150, 200, 0xE8, 0x03}; /* 1000 ms */
    cmd(b, j, sizeof(j));
    CHECK(b->lights.ring[0] == 100 && b->lights.ring[1] == 150 && b->lights.ring[2] == 200);
    CHECK(b->lights.fade_ms == 1000 && b->lights.seq == 7);
    CHECK(b->r_count == 1);
    uint8_t pkt[SKY_PACKET_SIZE];
    CHECK(sky_base_next_packet(b, pkt) && pkt[0] == 'J');

    /* J with a silly duration is capped */
    const uint8_t j_long[] = {'J', 0, 1, 1, 1, 0xFF, 0xFF};
    cmd(b, j_long, sizeof(j_long));
    CHECK(b->lights.fade_ms == SKY_LIGHT_FADE_MAX_MS);
    CHECK(sky_base_next_packet(b, pkt) && pkt[0] == 'J');

    /* J for another side: acked, lights untouched */
    const uint8_t seq_before = (uint8_t)b->lights.seq;
    const uint8_t j_side[] = {'J', 2, 9, 9, 9, 1, 0};
    cmd(b, j_side, sizeof(j_side));
    CHECK((uint8_t)b->lights.seq == seq_before);
    CHECK(sky_base_next_packet(b, pkt) && pkt[0] == 'J');

    /* J of the wrong length: ignored entirely, as before (no ack) */
    const uint8_t j_bad[] = {'J', 0, 1, 2, 3};
    cmd(b, j_bad, sizeof(j_bad));
    CHECK(b->r_count == 0 && (uint8_t)b->lights.seq == seq_before);

    /* Light commands do not disturb the rest of the portal */
    const uint8_t a[] = {'A', 1};
    cmd(b, a, sizeof(a));
    CHECK(sky_base_next_packet(b, pkt) && pkt[0] == 0x41);

    /* NULL / empty input is harmless */
    sky_base_handle_command(b, NULL, 4);
    sky_base_handle_command(b, c, 0);

    free(b);
    puts("lights_test ok");
    return 0;
}
