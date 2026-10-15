#pragma once
/* Portal speaker audio: portable part (no OS, USB or hardware dependencies).
 *
 * The game streams 8 kHz, 16-bit signed little-endian PCM to the portal's interrupt OUT
 * endpoint. sky_audio_push_pcm16() (USB interrupt) converts it to unsigned 8-bit samples
 * (128 = silence) and queues them; sky_audio_pull() (DMA interrupt) takes them out again at
 * the playback rate. A single-producer / single-consumer ring: each index is written by
 * exactly one side, so no lock is needed. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKY_AUDIO_RATE 8000
#define SKY_AUDIO_RING 2048 /* samples (256 ms); power of two, < 65536 */
#define SKY_AUDIO_SILENCE 128

/* Gain in 1/16 steps (16 = 1.0x). */
#define SKY_AUDIO_GAIN_UNITY 16

typedef struct {
    volatile uint16_t head; /* free-running; written by the producer only */
    volatile uint16_t tail; /* free-running; written by the consumer only */
    volatile bool accept; /* producer drops everything while false */
    uint8_t gain; /* see SKY_AUDIO_GAIN_UNITY */
    volatile uint32_t pushed; /* samples queued so far (diagnostics) */
    volatile uint8_t peak; /* largest |sample - 128| queued so far (diagnostics) */
    uint8_t ring[SKY_AUDIO_RING];
} SkyAudio;

void sky_audio_init(SkyAudio* a);
void sky_audio_set_gain(SkyAudio* a, uint8_t gain);

/* Queue PCM16 LE bytes (an odd trailing byte is ignored). Samples that do not fit are
 * dropped. Returns the number of samples queued. */
size_t sky_audio_push_pcm16(SkyAudio* a, const uint8_t* bytes, size_t len);

/* Fill out[0..n) with queued samples; the rest (underrun) is SKY_AUDIO_SILENCE.
 * Returns the number of real samples delivered. */
size_t sky_audio_pull(SkyAudio* a, uint8_t* out, size_t n);

/* Test tone: n samples of a 440 Hz square wave (centred on SKY_AUDIO_SILENCE). `phase` keeps
 * the wave continuous between calls; start it at 0. */
void sky_audio_tone_fill(uint8_t* out, size_t n, uint32_t* phase);

/* Number of samples waiting. */
size_t sky_audio_count(const SkyAudio* a);

/* Drop everything queued. Only call while neither side is running. */
void sky_audio_flush(SkyAudio* a);

#ifdef __cplusplus
}
#endif
