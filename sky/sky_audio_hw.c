/* Plays the portal's speaker audio on the Flipper's speaker.
 *
 * TIM16 (the speaker's timer, output on PB8) runs as a PWM DAC: 64 MHz / 250 steps per
 * period, with the repetition counter set so that one update event (and one DMA request) happens every
 * 32 periods = exactly 8000 times a second. Each update event, DMA1 channel 1 copies the next
 * sample into the compare register. The DMA buffer is circular; half-transfer and
 * transfer-complete interrupts refill the half that just played from the audio ring.
 *
 * NOT built or run against the real SDK headers in the environment this was written in: the
 * LL/HAL calls follow the STM32WB LL API and the way other Flipper apps drive the speaker, but
 * check them against your firmware (Momentum) when you build. */
#include "sky_audio_hw.h"
#include <furi.h>
#include <furi_hal.h>
#include <stdlib.h>
#include <string.h>
#include <stm32wbxx_ll_bus.h>
#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_dmamux.h>
#include <stm32wbxx_ll_tim.h>

#define SPK_TIMER TIM16
#define SPK_CHANNEL LL_TIM_CHANNEL_CH1
#define SPK_DMA DMA1, LL_DMA_CHANNEL_1

#define PWM_REPEAT 32 /* PWM periods per sample */
#define DMA_SAMPLES 256 /* circular buffer, 32 ms; refilled in halves of 16 ms */

static SkyAudio* s_audio = NULL;
static uint16_t* s_dma_buf = NULL;
static uint32_t s_pwm_top = 0; /* auto-reload: PWM runs 0..s_pwm_top */
static volatile bool s_running = false;
static volatile uint32_t s_tone_left = 0; /* test tone samples still to play */
static uint32_t s_tone_phase = 0;
static volatile uint32_t s_refills = 0;

/* 0..255 sample (128 = silence) -> compare value 0..s_pwm_top */
static inline uint16_t to_compare(uint8_t v) {
    return (uint16_t)(((uint32_t)v * (s_pwm_top + 1)) >> 8);
}

static void fill_half(uint16_t* dst) {
    uint8_t tmp[DMA_SAMPLES / 2];
    s_refills++;
    if(s_tone_left > 0) {
        const size_t n = s_tone_left < sizeof(tmp) ? s_tone_left : sizeof(tmp);
        sky_audio_tone_fill(tmp, n, &s_tone_phase);
        for(size_t i = n; i < sizeof(tmp); i++) tmp[i] = SKY_AUDIO_SILENCE;
        s_tone_left -= n;
    } else {
        sky_audio_pull(s_audio, tmp, sizeof(tmp)); /* underrun -> silence */
    }
    for(size_t i = 0; i < sizeof(tmp); i++) dst[i] = to_compare(tmp[i]);
}

static void sky_audio_dma_isr(void* ctx) {
    UNUSED(ctx);
    if(!s_audio || !s_dma_buf) return;
    if(LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        fill_half(s_dma_buf);
    }
    if(LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        fill_half(s_dma_buf + DMA_SAMPLES / 2);
    }
}

bool sky_audio_hw_running(void) {
    return s_running;
}

void sky_audio_hw_tone(uint32_t ms) {
    if(!s_running) return;
    s_tone_phase = 0;
    s_tone_left = ms * (SKY_AUDIO_RATE / 1000);
}

bool sky_audio_hw_tone_active(void) {
    return s_running && s_tone_left > 0;
}

uint32_t sky_audio_hw_refills(void) {
    return s_refills;
}

bool sky_audio_hw_start(SkyAudio* audio) {
    if(s_running) return true;
    if(!furi_hal_speaker_acquire(100)) return false; /* acquire also clocks TIM16 and sets the pin */

    s_dma_buf = malloc(DMA_SAMPLES * sizeof(uint16_t));
    if(!s_dma_buf) {
        furi_hal_speaker_release();
        return false;
    }
    s_audio = audio;
    s_tone_left = 0;
    s_refills = 0;
    s_pwm_top = (SystemCoreClock / (SKY_AUDIO_RATE * PWM_REPEAT)) - 1; /* 249 at 64 MHz */
    for(size_t i = 0; i < DMA_SAMPLES; i++) s_dma_buf[i] = to_compare(SKY_AUDIO_SILENCE);

    /* Timer: PWM on CH1, one update event per PWM_REPEAT periods */
    LL_TIM_InitTypeDef tim = {0};
    tim.Prescaler = 0;
    tim.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim.Autoreload = s_pwm_top;
    tim.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    tim.RepetitionCounter = PWM_REPEAT - 1;
    LL_TIM_Init(SPK_TIMER, &tim);
    LL_TIM_EnableARRPreload(SPK_TIMER);

    LL_TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = LL_TIM_OCMODE_PWM1;
    oc.OCState = LL_TIM_OCSTATE_ENABLE;
    oc.OCPolarity = LL_TIM_OCPOLARITY_HIGH;
    oc.CompareValue = s_dma_buf[0];
    LL_TIM_OC_Init(SPK_TIMER, SPK_CHANNEL, &oc);
    LL_TIM_OC_EnablePreload(SPK_TIMER, SPK_CHANNEL);
    LL_TIM_EnableAllOutputs(SPK_TIMER); /* TIM16 needs the main output enable */
    LL_TIM_GenerateEvent_UPDATE(SPK_TIMER); /* load ARR / repetition counter / compare */
    LL_TIM_ClearFlag_UPDATE(SPK_TIMER);

    /* DMA1 ch1: buffer -> TIM16 compare register, one halfword per update event */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1 | LL_AHB1_GRP1_PERIPH_DMAMUX1);
    LL_DMA_DisableChannel(SPK_DMA);
    LL_DMA_SetDataTransferDirection(SPK_DMA, LL_DMA_DIRECTION_MEMORY_TO_PERIPH); /* sets the DIR bit */
    LL_DMA_ConfigAddresses(
        SPK_DMA,
        (uint32_t)s_dma_buf,
        (uint32_t)&SPK_TIMER->CCR1,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_SetDataLength(SPK_DMA, DMA_SAMPLES);
    LL_DMA_SetPeriphRequest(SPK_DMA, LL_DMAMUX_REQ_TIM16_UP);
    LL_DMA_SetChannelPriorityLevel(SPK_DMA, LL_DMA_PRIORITY_VERYHIGH);
    LL_DMA_SetMode(SPK_DMA, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(SPK_DMA, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(SPK_DMA, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(SPK_DMA, LL_DMA_PDATAALIGN_HALFWORD);
    LL_DMA_SetMemorySize(SPK_DMA, LL_DMA_MDATAALIGN_HALFWORD);
    LL_DMA_ClearFlag_HT1(DMA1);
    LL_DMA_ClearFlag_TC1(DMA1);
    LL_DMA_EnableIT_HT(SPK_DMA);
    LL_DMA_EnableIT_TC(SPK_DMA);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, sky_audio_dma_isr, NULL);

    s_running = true;
    LL_DMA_EnableChannel(SPK_DMA);
    LL_TIM_EnableDMAReq_UPDATE(SPK_TIMER);
    LL_TIM_EnableCounter(SPK_TIMER);
    return true;
}

void sky_audio_hw_stop(void) {
    if(!s_running) return;
    s_running = false;
    LL_TIM_DisableDMAReq_UPDATE(SPK_TIMER);
    LL_TIM_DisableCounter(SPK_TIMER);
    LL_TIM_DisableAllOutputs(SPK_TIMER);
    LL_DMA_DisableIT_HT(SPK_DMA);
    LL_DMA_DisableIT_TC(SPK_DMA);
    LL_DMA_DisableChannel(SPK_DMA);
    LL_DMA_ClearFlag_HT1(DMA1);
    LL_DMA_ClearFlag_TC1(DMA1);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, NULL, NULL);
    furi_hal_speaker_release();
    free(s_dma_buf);
    s_dma_buf = NULL;
    s_audio = NULL;
}
