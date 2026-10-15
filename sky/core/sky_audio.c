#include "sky_audio.h"
#include <string.h>

/* The ring data must be written before head moves and read before tail moves. Both sides run
 * on one core (the USB and DMA interrupts), so a compiler barrier is all that is needed. */
#define SKY_BARRIER() __asm__ volatile("" ::: "memory")

void sky_audio_init(SkyAudio* a) {
    memset(a, 0, sizeof(*a));
    a->gain = SKY_AUDIO_GAIN_UNITY;
}

void sky_audio_set_gain(SkyAudio* a, uint8_t gain) {
    a->gain = gain;
}

size_t sky_audio_count(const SkyAudio* a) {
    return (uint16_t)(a->head - a->tail);
}

size_t sky_audio_push_pcm16(SkyAudio* a, const uint8_t* bytes, size_t len) {
    if(!a || !bytes) return 0;
    uint16_t head = a->head;
    const uint16_t tail = a->tail;
    size_t free_slots = SKY_AUDIO_RING - (uint16_t)(head - tail);
    size_t queued = 0;
    for(size_t i = 0; i + 1 < len && queued < free_slots; i += 2) {
        const int16_t s = (int16_t)((uint16_t)bytes[i] | ((uint16_t)bytes[i + 1] << 8));
        int32_t x = ((int32_t)s * a->gain) >> 12; /* s / 256 (to 8 bit) * gain / 16 */
        if(x > 127) x = 127;
        if(x < -128) x = -128;
        a->ring[head % SKY_AUDIO_RING] = (uint8_t)(x + SKY_AUDIO_SILENCE);
        const uint8_t mag = (uint8_t)(x < 0 ? -x : x);
        if(mag > a->peak) a->peak = mag > 127 ? 127 : mag;
        head++;
        queued++;
    }
    SKY_BARRIER();
    a->head = head;
    a->pushed += (uint32_t)queued;
    return queued;
}

size_t sky_audio_pull(SkyAudio* a, uint8_t* out, size_t n) {
    uint16_t tail = a->tail;
    const uint16_t head = a->head;
    size_t avail = (uint16_t)(head - tail);
    size_t take = avail < n ? avail : n;
    SKY_BARRIER();
    for(size_t i = 0; i < take; i++) {
        out[i] = a->ring[tail % SKY_AUDIO_RING];
        tail++;
    }
    for(size_t i = take; i < n; i++) out[i] = SKY_AUDIO_SILENCE;
    SKY_BARRIER();
    a->tail = tail;
    return take;
}

void sky_audio_tone_fill(uint8_t* out, size_t n, uint32_t* phase) {
    const uint32_t step = (440u * 65536u) / SKY_AUDIO_RATE; /* Q16 phase per sample */
    for(size_t i = 0; i < n; i++) {
        *phase = (*phase + step) & 0xFFFFu;
        out[i] = (uint8_t)(*phase & 0x8000u ? SKY_AUDIO_SILENCE + 100 : SKY_AUDIO_SILENCE - 100);
    }
}

void sky_audio_flush(SkyAudio* a) {
    a->tail = a->head;
}
