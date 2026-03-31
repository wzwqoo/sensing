//
// Created by wzw on 3/4/26.
//

#include "dsp.h"
#include "stm32h7xx_hal.h"
#include "arm_math.h"
#include <math.h>

#include "adar7251.h"
#include "main.h"


// Initialization Function
void Init_fft(void) {
    // Initialize the Real FFT instance
    arm_rfft_fast_init_f32(&rfft_inst, FFT_SIZE);
}

// ---------------------------------------------------------
// 1. CORDIC Phase Calculation Helper
// ---------------------------------------------------------
float Get_Phase_CORDIC(float I, float Q) {
    // Normalize I and Q to prevent Q1.31 overflow, preserving the angle
    float max_val = fmaxf(fabsf(I), fabsf(Q));
    if (max_val == 0.0f) return 0.0f;

    I = I / max_val;
    Q = Q / max_val;

    // Convert to Q1.31 format for Hardware CORDIC
    // CORDIC input format for Phase:[X, Y]
    int32_t cordic_in[2];
    arm_float_to_q31(&I, &cordic_in[0], 1);
    arm_float_to_q31(&Q, &cordic_in[1], 1);

    int32_t cordic_out[1]; // Output is Phase in Q1.31

    // Hardware CORDIC block calculation
    // Configured via CubeMX for Phase Calculation (ATAN2)
    HAL_CORDIC_Calculate(&hcordic, cordic_in, cordic_out, 1, HAL_MAX_DELAY);

    // Convert Q1.31 phase back to float [-pi, pi]
    // In Q1.31, 0x80000000 = -pi, 0x7FFFFFFF = +pi
    float phase_rad;
    arm_q31_to_float(cordic_out, &phase_rad, 1);
    phase_rad *= PI;

    return phase_rad;
}

// ---------------------------------------------------------
// 2. CFAR Detection
// ---------------------------------------------------------
int Run_1D_CA_CFAR(float* magnitudes, uint16_t length, RadarTarget_t* targets, int max_targets) {
    int target_count = 0;

    int window_size = (CFAR_NUM_TRAIN * 2) + (CFAR_NUM_GUARD * 2) + 1;

    for (int i = window_size / 2; i < length - (window_size / 2); i++) {
        float noise_sum = 0;

        // Sum lagging training cells
        for (int j = i - CFAR_NUM_GUARD - CFAR_NUM_TRAIN; j < i - CFAR_NUM_GUARD; j++) {
            noise_sum += magnitudes[j];
        }
        // Sum leading training cells
        for (int j = i + CFAR_NUM_GUARD + 1; j <= i + CFAR_NUM_GUARD + CFAR_NUM_TRAIN; j++) {
            noise_sum += magnitudes[j];
        }

        float noise_avg = noise_sum / (CFAR_NUM_TRAIN * 2);
        float threshold = noise_avg * CFAR_ALPHA;

        // Cell Under Test (CUT)
        if (magnitudes[i] > threshold) {
            // Target detected!
            if (target_count < max_targets) {
                targets[target_count].bin_index = i;
                target_count++;
            }
        }
    }
    return target_count;
}

// ---------------------------------------------------------
// 3. Main Processing Pipeline
// ---------------------------------------------------------
void Process_Radar_Data_main() {
    RadarTarget_t targets[10];
    uint16_t out_size = SAMPLES_PER_FRAME;
    uint16_t input_size     = SAMPLES_PER_FRAME;

    // Phase 1: FIR Filtering (Hardware FMAC)
    for (int ch = 0; ch < NUM_RX_CHANNELS; ch++) {
        /* 1. Preload previous state for this channel */
        if (HAL_FMAC_FilterPreload(&hfmac,
                               rx_history[ch],     // Previous samples for this channel
                               HISTORY_SIZE,     // HISTORY_SIZE
                               NULL, 0) != HAL_OK)
        {
            Error_Handler();
        }
        /* 2. Configure OUTPUT (Read DMA) FIRST */
        // This tells the FMAC where to push the data via DMA
        if (HAL_FMAC_FilterStart(&hfmac, rx_filtered[ch], &out_size) != HAL_OK)
        {
            Error_Handler();
        }

        /* 3. Configure INPUT (Write DMA) NEXT */
        // This feeds the input buffer via DMA and actually starts the processing
        if (HAL_FMAC_AppendFilterData(&hfmac, rx_raw[ch], &input_size) != HAL_OK)
        {
            Error_Handler();
        }

        /* 4. Wait for DMA Transfer Complete */
        // The FMAC state will return to READY when both Read and Write DMAs finish
        // In a real application, you could yield the RTOS task here instead of a blocking while-loop
        while(HAL_FMAC_GetState(&hfmac) != HAL_FMAC_STATE_READY)
        {
            // Waiting for the interrup to update to ready
        }

        /* 5. Save the last few samples of the output/input to 'aHistory[ch]'
           so the NEXT time this channel is processed, the filter is continuous */
        memcpy(rx_history[ch], &rx_raw[ch][SAMPLES_PER_FRAME - HISTORY_SIZE], HISTORY_SIZE * sizeof(int16_t));
    }

    // Phase 2: FFT (CMSIS-DSP / Hardware FPU)
    for (int ch = 0; ch < NUM_RX_CHANNELS; ch++) {
        // Convert Q1.15 filtered output to Float32 for FPU FFT
        arm_q15_to_float(rx_filtered[ch], fft_in, NUM_SAMPLES);

        // Perform Real FFT
        // Output format:[Real0, Imag0, Real1, Imag1, ... Real(N/2-1), Imag(N/2-1)]
        arm_rfft_fast_f32(&rfft_inst, fft_in, fft_out[ch], 0);
    }

    // Calculate Magnitudes of Channel 0 for CFAR Detection
    // arm_cmplx_mag_f32 uses FPU instructions optimized for Cortex-M7
    arm_cmplx_mag_f32(fft_out[0], fft_mag, FFT_SIZE / 2);

    // Phase 3: CFAR Target Detection
    int num_targets = Run_1D_CA_CFAR(fft_mag, FFT_SIZE / 2, targets, 10);

    // Phase 4: Compute Speed and Delta Phase (Hardware CORDIC)
    for (int i = 0; i < num_targets; i++) {
        uint32_t bin = targets[i].bin_index;

        // --- Calculate Speed (Doppler Formula) ---
        // Doppler Frequency = bin * (Sample Rate / FFT_SIZE)
        float doppler_freq = (float)bin * (SAMPLE_RATE_HZ / (float)FFT_SIZE);
        // Speed = (Doppler Freq * Wavelength) / 2
        targets[i].speed_mps = (doppler_freq * RADAR_WAVELENGTH_M) / 2.0f;

        // --- Calculate Delta Phase ---
        // Extract Complex Values (I, Q) for the detected target bin
        // Note: CMSIS-DSP stores complex numbers as consecutive pairs [Real, Imag]
        float I1 = fft_out[0][bin * 2];
        float Q1 = fft_out[0][bin * 2 + 1];
        float I2 = fft_out[1][bin * 2];
        float Q2 = fft_out[1][bin * 2 + 1];
        float I3 = fft_out[2][bin * 2];
        float Q3 = fft_out[2][bin * 2 + 1];

        // Get absolute phases using CORDIC Accelerator
        float phase1 = Get_Phase_CORDIC(I1, Q1);
        float phase2 = Get_Phase_CORDIC(I2, Q2);
        float phase3 = Get_Phase_CORDIC(I3, Q3);

        targets[i].phase_rx1 = phase1;

        // Calculate Delta Phase and handle phase wrapping [-pi, pi]
        targets[i].delta_phase_1_2 = phase2 - phase1;
        if (targets[i].delta_phase_1_2 > PI)  targets[i].delta_phase_1_2 -= 2*PI;
        if (targets[i].delta_phase_1_2 < -PI) targets[i].delta_phase_1_2 += 2*PI;

        targets[i].delta_phase_2_3 = phase3 - phase2;
        if (targets[i].delta_phase_2_3 > PI)  targets[i].delta_phase_2_3 -= 2*PI;
        if (targets[i].delta_phase_2_3 < -PI) targets[i].delta_phase_2_3 += 2*PI;
    }
}