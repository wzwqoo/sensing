/************************************************************************
 *
 * defADAR7251.h
 *
 * (c) Copyright 2015 Analog Devices, Inc.  All rights reserved.
 *
 ************************************************************************/
#ifndef ADAR7251_H_
#define ADAR7251_H_

#include <stdint.h>
#include "stm32h7xx_hal.h"


extern SPI_HandleTypeDef hspi4;   // For Control (Registers)
extern PSSI_HandleTypeDef hpssi;
extern TIM_HandleTypeDef htim12; // For CONV_START Sync

// 1. Clocking & PLL Control
#define REG_CLK_CTRL            0x0000  // Clock Control (PLL Bypass)
#define REG_PLL_DEN             0x0001  // PLL Denominator (M)
#define REG_PLL_NUM             0x0002  // PLL Numerator (N)
#define REG_PLL_CTRL            0x0003  // PLL Control (Multipliers/Dividers)
#define REG_PLL_LOCK            0x0005  // PLL Status (Read Only)
// 2. Global Chip Control
#define REG_MASTER_ENABLE       0x0040  // Master Enable Switch
#define REG_ADC_ENABLE          0x0041  // Individual ADC Channel Enablers
#define REG_POWER_ENABLE        0x0042  // Internal Block Power Control
// 3. ASIL Safety & Error Monitoring
#define REG_ASIL_CLEAR          0x0080  // Clear ASIL Errors
#define REG_ASIL_MASK           0x0081  // Mask specific ASIL Errors
#define REG_ASIL_FLAG           0x0082  // ASIL Error Flag
#define REG_ASIL_ERROR          0x0083  // ASIL Error Code (Read this on Fault)
#define REG_CRC_VALUE_L         0x0084  // Last SPI Transaction CRC (Low)
#define REG_CRC_VALUE_H         0x0085  // Last SPI Transaction CRC (High)
#define REG_RM_CRC_ENABLE       0x0086  // Register Map CRC Start
#define REG_RM_CRC_DONE         0x0087  // Register Map CRC Calculation Status
#define REG_RM_CRC_VALUE_L      0x0088  // Register Map CRC Result (Low)
#define REG_RM_CRC_VALUE_H      0x0089  // Register Map CRC Result (High)
// 4. Analog Front End (AFE) Settings
#define REG_LNA_GAIN            0x0100  // Low Noise Amplifier Gain Control
#define REG_PGA_GAIN            0x0101  // Programmable Gain Amplifier Control
#define REG_ADC_ROUTING1_4      0x0102  // Signal Path Routing / Input Swapping
// 5. Digital Filters & DAQ Mode
#define REG_DECIM_RATE          0x0140  // Decimator Rate (Sample Rate Selection)
#define REG_HIGH_PASS           0x0141  // High Pass Filter/Equalizer Enable
#define REG_ACK_MODE            0x0143  // DAQ Mode Control (Continuous vs DAQ)
#define REG_TRUNCATE_MODE       0x0144  // Decimator Truncation Method
// 6. Data Interface Port Configuration
#define REG_SERIAL_MODE         0x01C0  // Serial Output Port Settings (TDM/I2S)
#define REG_PARALLEL_MODE       0x01C1  // Parallel Port Settings (Byte/Nibble)
#define REG_OUTPUT_MODE         0x01C2  // Interface Select (Serial vs Parallel)
// 7. Auxiliary ADC (Housekeeping)
#define REG_ADC_READ0           0x0200  // Aux ADC Value 0
#define REG_ADC_READ1           0x0201  // Aux ADC Value 1
#define REG_ADC_SPEED           0x0210  // Aux ADC Sample Rate
#define REG_ADC_MODE            0x0211  // Aux ADC Input Selection
// 8. GPIO / Multi-Purpose Pins
#define REG_MP0_MODE            0x0250  // GPIO 1 Function Mode
#define REG_MP1_MODE            0x0251  // GPIO 2 Function Mode
#define REG_MP0_WRITE           0x0260  // GPIO 1 Output Value
#define REG_MP1_WRITE           0x0261  // GPIO 2 Output Value
#define REG_MP0_READ            0x0270  // GPIO 1 Input Value
#define REG_MP1_READ            0x0271  // GPIO 2 Input Value
// 9. Pin Drive Strength & Pull-up/down
#define REG_SPI_CLK_PIN         0x0280  // SPI_CLK Drive/Pull
#define REG_MISO_PIN            0x0281  // SPI_MISO Drive/Pull
#define REG_SS_PIN              0x0282  // SPI_SS Drive/Pull
#define REG_MOSI_PIN            0x0283  // SPI_MOSI Drive/Pull
#define REG_ADDR15_PIN          0x0284  // ADDR15 Drive/Pull
#define REG_FAULT_PIN           0x0285  // FAULT Drive/Pull
#define REG_FS_ADC_PIN          0x0286  // FS_ADC Drive/Pull
#define REG_CS_PIN              0x0287  // CONV_START Drive/Pull
#define REG_SCLK_ADC_PIN        0x0288  // SCLK_ADC Drive/Pull
#define REG_ADC_DOUT0_PIN       0x0289  // DOUT0 Drive/Pull
#define REG_ADC_DOUT1_PIN       0x028A  // DOUT1 Drive/Pull
#define REG_ADC_DOUT2_PIN       0x028B  // DOUT2 Drive/Pull
#define REG_ADC_DOUT3_PIN       0x028C  // DOUT3 Drive/Pull
#define REG_ADC_DOUT4_PIN       0x028D  // DOUT4 Drive/Pull
#define REG_ADC_DOUT5_PIN       0x028E  // DOUT5 Drive/Pull
#define REG_DATA_READY_PIN      0x0291  // DATA_READY Drive/Pull
#define REG_XTAL_CTRL           0x0292  // Crystal Oscillator Enable/Drive
// 10. Performance & Test Registers
#define REG_ADC_SETTING1        0x0301  // Peak Detect Enable (Overload Detect)
#define REG_ADC_SETTING2        0x0308  // Performance Improvement 2
#define REG_ADC_SETTING3        0x030A  // Performance Improvement 3
#define REG_DEJITTER_WINDOW     0x030E  // Digital Filter Sync Enable
#define REG_CRC_EN              0xFD00  // Main SPI CRC Global Enable/Disable

#define SPI1_CS_GPIO_Port    GPIOE
#define SPI1_ADAR_CS_Pin     GPIO_PIN_11
#define ADAR_RESET_GPIO_Port GPIOE
#define ADAR_RESET_Pin       GPIO_PIN_9

// If your ADC is at 1.2 MSPS and you set your buffer to 1024 samples: 0.85ms per sample
#define SAMPLES_PER_FRAME	 1024
#define TOTAL_BUF_SIZE (SAMPLES_PER_FRAME * 2 * 2 * 4)//16 bit * double buffer * 4 channels interleaved

/* Fault Bitmask for Reg 0x083 */
#define FAULT_VCO_ERROR            (1 << 0)
#define FAULT_CLOCK_ERROR          (1 << 1)
#define FAULT_OVER_TEMP            (1 << 2)

// Large buffer in D2 RAM
ALIGN_32BYTES(uint8_t radar_stream_buf[TOTAL_BUF_SIZE])__attribute__((section(".RAM_D1")));
ALIGN_32BYTES(uint8_t usb_hs_tx_buf[512]) __attribute__((section(".RAM_D2")));

int16_t rx_raw[3][SAMPLES_PER_FRAME]; // Temporary de-interleaved buffers

void Init_ADAR7251();
void ADAR7251_Begin_Data_Capture();
void My_DMA_RxHalfCpltCallback(DMA_HandleTypeDef *hdma);
void My_DMA_PSSI_RxCpltCallback(DMA_HandleTypeDef *hdma);
void ADAR7251_Emergency_Shutdown(void);


#endif