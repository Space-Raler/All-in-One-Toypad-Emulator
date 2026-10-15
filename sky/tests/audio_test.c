#include "sky_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void pcm(uint8_t* out, int16_t s) {
    out[0] = (uint8_t)(s & 0xFF);
    out[1] = (uint8_t)((uint16_t)s >> 8);
}

int main(void) {
    static SkyAudio a;
    uint8_t in[8], out[8];

    /* silence in -> 128 out, unity gain */
    sky_audio_init(&a);
    pcm(in, 0);
    assert(sky_audio_push_pcm16(&a, in, 2) == 1);
    assert(sky_audio_count(&a) == 1);
    assert(sky_audio_pull(&a, out, 1) == 1 && out[0] == 128);

    /* unity gain keeps the top 8 bits: 0x4000 -> +64, -0x4000 -> -64 */
    pcm(in, 0x4000);
    pcm(in + 2, -0x4000);
    assert(sky_audio_push_pcm16(&a, in, 4) == 2);
    assert(sky_audio_pull(&a, out, 2) == 2);
    assert(out[0] == 128 + 64 && out[1] == 128 - 64);

    /* gain 4x doubles twice, and clips at the rails */
    sky_audio_set_gain(&a, 4 * SKY_AUDIO_GAIN_UNITY);
    pcm(in, 0x1000); /* +16 at unity -> +64 */
    pcm(in + 2, 0x7FFF); /* clip high */
    pcm(in + 4, -0x8000); /* clip low */
    assert(sky_audio_push_pcm16(&a, in, 6) == 3);
    assert(sky_audio_pull(&a, out, 3) == 3);
    assert(out[0] == 128 + 64 && out[1] == 255 && out[2] == 0);

    /* odd trailing byte ignored */
    sky_audio_set_gain(&a, SKY_AUDIO_GAIN_UNITY);
    assert(sky_audio_push_pcm16(&a, in, 3) == 1);
    sky_audio_flush(&a);
    assert(sky_audio_count(&a) == 0);

    /* underrun is padded with silence and reports the real count */
    pcm(in, 0x2000);
    sky_audio_push_pcm16(&a, in, 2);
    memset(out, 0, sizeof(out));
    assert(sky_audio_pull(&a, out, 4) == 1);
    assert(out[0] == 128 + 32 && out[1] == 128 && out[3] == 128);

    /* overflow drops the newest; ring wrap keeps order */
    sky_audio_init(&a);
    uint8_t big[SKY_AUDIO_RING * 2 + 8];
    for(size_t i = 0; i < SKY_AUDIO_RING + 4; i++) pcm(big + i * 2, (int16_t)((i % 100) << 8));
    assert(sky_audio_push_pcm16(&a, big, sizeof(big) - 8 + 8) == SKY_AUDIO_RING);
    assert(sky_audio_count(&a) == SKY_AUDIO_RING);
    uint8_t chunk[256];
    size_t got = 0, idx = 0;
    while(got < SKY_AUDIO_RING) {
        size_t n = sky_audio_pull(&a, chunk, sizeof(chunk));
        for(size_t k = 0; k < n; k++, idx++) assert(chunk[k] == 128 + (idx % 100));
        got += n;
    }
    /* indices now wrap past 65535 safely: push/pull many times */
    for(int r = 0; r < 100; r++) {
        for(size_t i = 0; i < 1000; i++) pcm(big + i * 2, 0x0100);
        assert(sky_audio_push_pcm16(&a, big, 2000) == 1000);
        assert(sky_audio_pull(&a, chunk, 256) == 256);
        sky_audio_flush(&a);
    }
    /* diagnostics: pushed / peak, and the test tone alternates around 128 at ~440 Hz */
    sky_audio_init(&a);
    pcm(in, 0x2000);
    sky_audio_push_pcm16(&a, in, 2);
    assert(a.pushed == 1 && a.peak == 32);
    uint8_t tone[800];
    uint32_t ph = 0;
    sky_audio_tone_fill(tone, sizeof(tone), &ph);
    int edges = 0;
    for(size_t i = 1; i < sizeof(tone); i++) {
        assert(tone[i] == 128 + 100 || tone[i] == 128 - 100);
        if(tone[i] != tone[i - 1]) edges++;
    }
    assert(edges >= 85 && edges <= 91); /* 100 ms of 440 Hz = 44 cycles = ~88 edges */
    puts("audio_test ok");
    return 0;
}
