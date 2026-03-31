//
// Created by wzw on 10/28/25.
//

#include "adc.h"

#include "cmsis_os2.h"
#include "stm32f7xx_hal_adc.h"
#include "usbd_cdc_if.h"

uint16_t adc1_dma_buf[ADC_BUF_LEN];
uint16_t adc2_dma_buf[ADC_BUF_LEN];

uint16_t adc1_ch1_data[SAMPLES_PER_CHANNEL];  // Temporary storage for deinterleaving
uint16_t adc1_ch2_data[SAMPLES_PER_CHANNEL];
uint16_t adc2_ch1_data[SAMPLES_PER_CHANNEL];
uint16_t adc2_ch2_data[SAMPLES_PER_CHANNEL];

static uint32_t adc1_sequence = 0;
static uint32_t adc2_sequence = 0;

volatile buffer_state_t adc1_buffer_state = BUFFER_STATE_IDLE; // Use volatile for shared vars
volatile buffer_state_t adc2_buffer_state = BUFFER_STATE_IDLE; // Use volatile for shared vars
volatile int usb_is_busy = 0;
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;


void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC1) {
        for (uint32_t i = 0; i < SAMPLES_PER_CHANNEL; i++) {
            adc1_ch1_data[i] = adc1_dma_buf[i * 2];      // Even indices: Channel 1
            adc1_ch2_data[i] = adc1_dma_buf[i * 2 + 1];  // Odd indices: Channel 2
        }
        adc1_buffer_state = BUFFER_STATE_HALF_READY;
    }
    else if (hadc->Instance == ADC2) {
        for (uint32_t i = 0; i < SAMPLES_PER_CHANNEL; i++) {
            adc2_ch1_data[i] = adc2_dma_buf[i * 2];      // Even indices: Channel 1
            adc2_ch2_data[i] = adc2_dma_buf[i * 2 + 1];  // Odd indices: Channel 2
        }
        adc2_buffer_state = BUFFER_STATE_HALF_READY;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC1) {
        for (uint32_t i = 0; i < SAMPLES_PER_CHANNEL; i++) {
            adc1_ch1_data[i] = adc1_dma_buf[ADC_BUF_HALFFULL+ i * 2];      // Even indices: Channel 1
            adc1_ch2_data[i] = adc1_dma_buf[ADC_BUF_HALFFULL+ i * 2 + 1];  // Odd indices: Channel 2
        }
        adc1_buffer_state = BUFFER_STATE_FULL_READY;
    }
    else if (hadc->Instance == ADC2) {
        for (uint32_t i = 0; i < SAMPLES_PER_CHANNEL; i++) {
            adc2_ch1_data[i] = adc2_dma_buf[ADC_BUF_HALFFULL+ i * 2];      // Even indices: Channel 1
            adc2_ch2_data[i] = adc2_dma_buf[ADC_BUF_HALFFULL+ i * 2 + 1];  // Odd indices: Channel 2
        }
        adc2_buffer_state = BUFFER_STATE_FULL_READY;
    }
}

void send_adc1_packet() {
    adc_packet_t packet;

    // Fill packet header
    packet.sync_marker = ADC1_SYNC_MARKER;
    packet.sequence_number = adc1_sequence++;

    // Copy channel data
    memcpy(packet.channel1_data, adc1_ch1_data, SAMPLES_PER_CHANNEL * sizeof(uint16_t));
    memcpy(packet.channel2_data, adc1_ch2_data, SAMPLES_PER_CHANNEL * sizeof(uint16_t));

    // Send via USB
    usb_is_busy = 1;
    CDC_Transmit_FS((uint8_t*)&packet, sizeof(packet));
}

void send_adc2_packet() {
    adc_packet_t packet;

    // Fill packet header
    packet.sync_marker = ADC2_SYNC_MARKER;
    packet.sequence_number = adc2_sequence++;

    // Copy channel data
    memcpy(packet.channel1_data, adc2_ch1_data, SAMPLES_PER_CHANNEL * sizeof(uint16_t));
    memcpy(packet.channel2_data, adc2_ch2_data, SAMPLES_PER_CHANNEL * sizeof(uint16_t));

    // Send via USB
    usb_is_busy = 1;
    CDC_Transmit_FS((uint8_t*)&packet, sizeof(packet));
}

void adc_task(void *argument) {
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc1_dma_buf, ADC_BUF_LEN);
    HAL_ADC_Start_DMA(&hadc2, (uint32_t*)adc2_dma_buf, ADC_BUF_LEN);
    while (1) {
        if (usb_is_busy == 0) {
            // Check if a buffer half is ready AND the USB is free
            if (adc1_buffer_state != BUFFER_STATE_IDLE) {
                usb_is_busy = 1; // Mark USB as busy
                send_adc1_packet();
                adc1_buffer_state = BUFFER_STATE_IDLE; // Mark buffer as processed
            }

            if (adc2_buffer_state != BUFFER_STATE_IDLE) {
                usb_is_busy = 1; // Mark USB as busy
                send_adc2_packet();
                adc2_buffer_state = BUFFER_STATE_IDLE; // Mark buffer as processed
            }
        }
        osDelay(1);  // Important: Yield to other tasks
    }
}// clear usb_is_busy in usbd_cdc_if.c

