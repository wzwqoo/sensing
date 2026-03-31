//
// Created by wzw on 10/28/25.
//

#include "stm32f7xx_hal_tim.h"
#include "dac.h"

// Triangle wave lookup table for FMCW
uint16_t chirp_waveform[SAMPLES_PER_CHIRP];

void generate_sawtooth_chirp(void) {
    for (int i = 0; i < SAMPLES_PER_CHIRP; i++) {
        chirp_waveform[i] = CHIRP_MIN + (i * CHIRP_AMPLITUDE) / SAMPLES_PER_CHIRP;
    }
    // Add flyback or use reset for periodic chirp
}

void generate_triangle_chirp(void) {
    for (int i = 0; i < SAMPLES_PER_CHIRP; i++) {
        if (i < SAMPLES_PER_CHIRP/2) {
            // Up-chirp
            chirp_waveform[i] = CHIRP_MIN + (i * CHIRP_AMPLITUDE * 2) / SAMPLES_PER_CHIRP;
        } else {
            // Down-chirp
            chirp_waveform[i] = CHIRP_MAX - ((i - SAMPLES_PER_CHIRP/2) * CHIRP_AMPLITUDE * 2) / SAMPLES_PER_CHIRP;
        }
    }
}

// Start DAC with DMA for continuous chirp generation
void start_fmcw_chirp(void) {
    generate_sawtooth_chirp();

    // Configure timer for chirp rate
    htim6.Instance = TIM6;
    htim6.Init.Prescaler = 0;
    htim6.Init.Period = (CHIRP_TIMER_FREQ / (CHIRP_RATE * SAMPLES_PER_CHIRP)) - 1;
    HAL_TIM_Base_Init(&htim6);

    // Start DAC with DMA
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1,
                     (uint32_t*)chirp_waveform, SAMPLES_PER_CHIRP,
                     DAC_ALIGN_12B_R);

    // Start timer to trigger DAC updates
    HAL_TIM_Base_Start(&htim6);
}