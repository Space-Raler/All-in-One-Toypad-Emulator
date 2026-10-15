#pragma once
/* Flipper Zero speaker output for the portal audio (8 kHz PCM through PWM + DMA).
 * Start/stop from a normal thread, never from an interrupt. */
#include <stdbool.h>
#include "core/sky_audio.h"

/* Take the speaker and start playing samples from `audio`. Returns false if the speaker
 * is in use elsewhere. Safe to call when already running. */
bool sky_audio_hw_start(SkyAudio* audio);

/* Stop playback and give the speaker back. Safe to call when not running. */
void sky_audio_hw_stop(void);

bool sky_audio_hw_running(void);

/* Play a test tone for `ms` milliseconds (speaker must be running). */
void sky_audio_hw_tone(uint32_t ms);
bool sky_audio_hw_tone_active(void);

/* DMA buffer refills since start (about 62 per second while running): proves the DMA runs. */
uint32_t sky_audio_hw_refills(void);
