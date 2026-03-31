//
// Created by wzw on 3/4/26.
//

#ifndef SOFTWARE_DSP_H
#define SOFTWARE_DSP_H

#include <stdint.h>
#include "arm_math.h"
#include "stm32h7xx_hal_cordic.h"

// --- System Definitions ---
#define NUM_RX_CHANNELS   3
#define NUM_SAMPLES       1024
#define FFT_SIZE          1024 // no binning or zero padding here
#define FIR_TAPS          100
#define HISTORY_SIZE      (FIR_TAPS - 1)

// --- Radar System Parameters ---
#define RADAR_WAVELENGTH_M  0.03f   // Example: 10GHz radar
#define SAMPLE_RATE_HZ      1200000.0f // Example: 1.2 MSPS
#define PI                  3.14159265358979f

// --- CFAR Parameters ---
#define CFAR_NUM_TRAIN    8
#define CFAR_NUM_GUARD    2
#define CFAR_ALPHA        4.5f   // Threshold multiplier

// --- External Hardware Handles (From STM32CubeMX) ---
extern FMAC_HandleTypeDef hfmac;
extern CORDIC_HandleTypeDef hcordic;

// --- Buffers ---
// 1. Raw ADC Data (Q1.15 format) from adar7251 rx_raw
// 2. FMAC Output Data (Q1.15 format)
int16_t rx_filtered[NUM_RX_CHANNELS][NUM_SAMPLES];
int16_t rx_history[NUM_RX_CHANNELS][HISTORY_SIZE];

// 3. FFT Buffers (Float32 for FPU)
float32_t fft_in[NUM_SAMPLES];
float32_t fft_out[NUM_RX_CHANNELS][FFT_SIZE]; // Complex array: [Real0, Imag0, Real1, Imag1...]
float32_t fft_mag[FFT_SIZE / 2];              // Magnitudes for CFAR

// CMSIS-DSP FFT Instance
arm_rfft_fast_instance_f32 rfft_inst;

// Target structure
typedef struct {
    uint32_t bin_index;
    float speed_mps;
    float phase_rx1;
    float delta_phase_1_2;
    float delta_phase_2_3;
} RadarTarget_t;

void Init_fft(void);
float Get_Phase_CORDIC(float I, float Q);
int Run_1D_CA_CFAR(float* magnitudes, uint16_t length, RadarTarget_t* targets, int max_targets);
void Process_Radar_Data_main();

#endif //SOFTWARE_DSP_H