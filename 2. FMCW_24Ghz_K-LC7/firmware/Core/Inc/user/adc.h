//
// Created by wzw on 10/28/25.
//

#ifndef ADC_H
#define ADC_H

#define ADC_BUF_LEN 2048 // DMA buffer size (e.g., 200 samples)
#define ADC_BUF_HALFFULL 2048/2
#define SAMPLES_PER_CHANNEL 2048/4 //double buffer + 2 channels
#include <stdint.h>

typedef enum {
    BUFFER_STATE_IDLE,
    BUFFER_STATE_HALF_READY,
    BUFFER_STATE_FULL_READY
    } buffer_state_t;

typedef enum {
    CHANNEL_ADC1,
    CHANNEL_ADC2,
  } channel_id_t;

// #pragma pack(push, 1) // By default, C compilers add padding bytes to structures for memory alignment, this save current packing, set to 1-byte alignment
// without packing Memory: [AAAA][BPPP][SSSS] if i have adc_id
// Packet structure for one ADC (2 channels)
typedef struct {
    uint32_t sync_marker;          // 0xADC1F00D or 0xADC2F00D
    // uint8_t  adc_id;               // 1 or 2
    uint32_t sequence_number;      // Incremental counter
    uint16_t channel1_data[SAMPLES_PER_CHANNEL];
    uint16_t channel2_data[SAMPLES_PER_CHANNEL];
} adc_packet_t;
// #pragma pack(pop)

#define ADC1_SYNC_MARKER 0xADC1F00D
#define ADC2_SYNC_MARKER 0xADC2F00D

void adc_task(void *argument);

#endif //ADC_H
