//
// Created by wzw on 1/20/26.
//
#include "adar7251.h"
#include "adf4159.h"
#include "dsp.h"
#include "stm32h7xx_hal.h"
#include "usbd_cdc_if.h"

void ADAR7251_SPI_Write(uint16_t reg, uint16_t data) {
	uint8_t tx_buf[5];

	tx_buf[0] = 0x00; // Write Command
	tx_buf[1] = (reg >> 8) & 0xFF;
	tx_buf[2] = reg & 0xFF;
	tx_buf[3] = (data >> 8) & 0xFF;
	tx_buf[4] = data & 0xFF;

	HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_ADAR_CS_Pin, GPIO_PIN_RESET);// CS Low
	HAL_Delay(1);
	HAL_SPI_Transmit(&hspi4, tx_buf, 5, 100);
	HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_ADAR_CS_Pin, GPIO_PIN_SET);// CS High

	HAL_Delay(1);
}

uint16_t ADAR7251_SPI_Read(uint16_t reg) {
	uint8_t tx_buf[5] = {0x01, (reg >> 8), reg, 0, 0}; // R/W bit = 1
	uint8_t rx_buf[5] = {0};

	HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_ADAR_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_TransmitReceive(&hspi4, tx_buf, rx_buf, 5, 100);
	HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_ADAR_CS_Pin, GPIO_PIN_SET);

	return (rx_buf[3] << 8) | rx_buf[4];
}

void Init_ADAR7251(void) {
	// 1. Reset device (optional, if you have a Reset pin)
	HAL_GPIO_WritePin(ADAR_RESET_GPIO_Port, ADAR_RESET_Pin, GPIO_PIN_RESET);
	HAL_Delay(15);
	HAL_GPIO_WritePin(ADAR_RESET_GPIO_Port, ADAR_RESET_Pin, GPIO_PIN_SET);
	HAL_Delay(10); // Latching ADDR15 happens here

	// Disable CRC (Standard communication)
	ADAR7251_SPI_Write(REG_CRC_EN, 0x0000);

	// 2. Configure PLL, R=2, N=2, M=5, X= 1, fIN = 48Mhz, fOUT = 115.2, R+(N/M)/X = 2.4
	ADAR7251_SPI_Write(REG_CLK_CTRL, 0x0000); //disable bypass
	ADAR7251_SPI_Write(REG_PLL_DEN, 0x0005); //M
	ADAR7251_SPI_Write(REG_PLL_NUM, 0x0002); //N
	ADAR7251_SPI_Write(REG_PLL_CTRL, 0x1013); // 0001000000010011
	//Bits [15:11] (R): 00010 (Value 2) Bits [7:4] (X): 0001 (Value 1) Bit 1 (Type): 1 (Fractional Mode) Bit 0 (Enable): 1 (PLL Enabled)

	// Wait for PLL Lock (Polling loop)
	do {
		HAL_Delay(1);
	} while ((ADAR7251_SPI_Read(REG_PLL_LOCK) & 0x01) == 0); // Loop until Bit 0 is 1

	// 3. Enable Internal Logic
	ADAR7251_SPI_Write(REG_MASTER_ENABLE, 0x0001); // MASTER_EN
	ADAR7251_SPI_Write(REG_ADC_ENABLE, 0x00FF);    // Enable 4 Channels
	ADAR7251_SPI_Write(REG_POWER_ENABLE, 0x03DF);   // Power up LDO, AuxADC off, MP_EN on, different from GITHUB

	// --- 4. ASIL / Error Management ---
	ADAR7251_SPI_Write(REG_ASIL_CLEAR,    0x0000); // Clear error flags
	ADAR7251_SPI_Write(REG_ASIL_MASK,     0x0000); // Unmask all errors
	ADAR7251_SPI_Write(REG_RM_CRC_ENABLE, 0x0000); // Register Map CRC off

	// --- 5. Analog Front End (AFE) & Gains ---
    ADAR7251_SPI_Write(REG_LNA_GAIN,       0x00FF); // LNA Gain = 16dB (All Channels)
    ADAR7251_SPI_Write(REG_PGA_GAIN,       0x0055); // PGA Gain = 2.8dB (All Channels)
    ADAR7251_SPI_Write(REG_ADC_ROUTING1_4, 0x2222); // Bypass EQ, Straight Path

    // --- 6. Digital Filters & Sample Rate ---
    ADAR7251_SPI_Write(REG_DECIM_RATE,    0x0003); // 1.2 MSPS Sample Rate
    ADAR7251_SPI_Write(REG_HIGH_PASS,     0x0019); // High Pass Filter ON (shift 12 -> 46.6Hz, can be higher)
    ADAR7251_SPI_Write(REG_ACK_MODE,      0x0000); // Continuous Conversion Mode
    ADAR7251_SPI_Write(REG_TRUNCATE_MODE, 0x0002); // Normal Rounding

    // --- 7. Data Interface (PPI Mode for STM32 PSSI) ---
    ADAR7251_SPI_Write(REG_SERIAL_MODE,   0x0040); // Set ADC to Master Mode to provide SCLK_ADC, and 50/50 duty cyle clock
    ADAR7251_SPI_Write(REG_PARALLEL_MODE, 0x0004); // Byte Wide (8-bit), High Byte First, 4 channels
    ADAR7251_SPI_Write(REG_OUTPUT_MODE,   0x0001); // **CRITICAL**: Switch to Parallel Port

    // --- 8. Aux ADC & GPIOs ---
    ADAR7251_SPI_Write(REG_ADC_SPEED, 0x0000); // Aux ADC 112.5 kHz
    ADAR7251_SPI_Write(REG_ADC_MODE,  0x0000); // Sample both Aux Inputs
    ADAR7251_SPI_Write(REG_MP0_MODE,  0x0000); // MP0 Primary Function
    ADAR7251_SPI_Write(REG_MP1_MODE,  0x0000); // MP1 Primary Function
    ADAR7251_SPI_Write(REG_MP0_WRITE, 0x0000); // Set Output Low
    ADAR7251_SPI_Write(REG_MP1_WRITE, 0x0000); // Set Output Low

    // --- 9. Pin Drive Strength & Hardware Config ---
    ADAR7251_SPI_Write(REG_SPI_CLK_PIN,   0x0000); // Default
    ADAR7251_SPI_Write(REG_MISO_PIN,      0x0000); // Default
    ADAR7251_SPI_Write(REG_SS_PIN,        0x0004); // Pull-up enabled
    ADAR7251_SPI_Write(REG_MOSI_PIN,      0x0000); // Default
    ADAR7251_SPI_Write(REG_ADDR15_PIN,    0x0004); // Pull-down enabled
    ADAR7251_SPI_Write(REG_FAULT_PIN,     0x0004); // Pull-up enabled
    ADAR7251_SPI_Write(REG_FS_ADC_PIN,    0x0003); // High Drive (Data Bit 7)
    ADAR7251_SPI_Write(REG_CS_PIN,        0x0003); // High Drive (CONV_START)
    ADAR7251_SPI_Write(REG_SCLK_ADC_PIN,  0x0003); // High Drive (Data Clock)
    ADAR7251_SPI_Write(REG_ADC_DOUT0_PIN, 0x0003); // High Drive (Data Bit 0)
    ADAR7251_SPI_Write(REG_ADC_DOUT1_PIN, 0x0003); // High Drive (Data Bit 1)
    ADAR7251_SPI_Write(REG_ADC_DOUT2_PIN, 0x0003); // High Drive (Data Bit 2)
    ADAR7251_SPI_Write(REG_ADC_DOUT3_PIN, 0x0003); // High Drive (Data Bit 3)
    ADAR7251_SPI_Write(REG_ADC_DOUT4_PIN, 0x0003); // High Drive (Data Bit 4)
    ADAR7251_SPI_Write(REG_ADC_DOUT5_PIN, 0x0003); // High Drive (Data Bit 5)
    ADAR7251_SPI_Write(REG_DATA_READY_PIN,0x0003); // High Drive (PSSI_DE)
    ADAR7251_SPI_Write(REG_XTAL_CTRL,     0x0000); // Use MCLKIN pin

    // --- 10. Performance & Optimization ---
    ADAR7251_SPI_Write(REG_ADC_SETTING1,    0x0306); // Peak Detect ON(Overload detection) to ensure stable data., EQ HPF 32kHz
    ADAR7251_SPI_Write(REG_ADC_SETTING2,    0x0013); // ADI Recommended Perf Setting
    ADAR7251_SPI_Write(REG_ADC_SETTING3,    0x0001); // Performance Improvement Enable
    ADAR7251_SPI_Write(REG_DEJITTER_WINDOW, 0x0000); // Sync Disabled for CW Mode
}

/**
  * @brief Switch to 4-Line Mode and Start DMA Data Capture
  */
void ADAR7251_Begin_Data_Capture() {
	// 1. Clear PSSI flags and start DMA
	// We receive into raw_radar_data (8-bit array)
	HAL_PSSI_Receive_DMA(&hpssi, (uint32_t*)radar_stream_buf, TOTAL_BUF_SIZE);
	// normal sequence:DMA IRQ -> HAL Internal Wrapper -> HAL_PSSI_RxCpltCallback
	// this sequence:DMA IRQ -> Custom Callback
	hpssi.hdmarx->XferHalfCpltCallback = My_DMA_RxHalfCpltCallback;
	hpssi.hdmarx->XferCpltCallback = My_DMA_PSSI_RxCpltCallback;
	// 2. Start the Timer to pulse CONV_START (Pin 35)
	// This provides the sync edge for both the ADC and the PSSI_DE signal
	HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
}

/*
*If your STM32 detects the FAULT pin going Low, you should:
1. **Stop the Radar:Immediately stop the TIM12 and the PSSI DMA to prevent processing bad data.
2. **Identify the Cause:Read Register 0x083 (ASIL_ERROR)via SPI1 to see exactly which bit triggered the fault.
3. **Clear the Fault:Once the physical problem is fixed, you must write a 1 to Register 0x080 (ASIL_CLEAR)to pull the FAULT pin back High.
*/
void ADAR7251_Emergency_Shutdown(void) {
	// 1. STOP THE RADAR HARDWARE
	HAL_TIM_Base_Stop(&htim12);      // Stop the Ramp/Trigger timer
	HAL_PSSI_Abort_DMA(&hpssi);           // Stop the Parallel Data interface

	// 2. IDENTIFY THE CAUSE
	// Note: Ensure SPI is in Mode 1,1 for ADAR7251
	uint8_t error_code = ADAR7251_SPI_Read(REG_ASIL_ERROR);

	printf("!!! RADAR FAULT DETECTED !!!\n");
	if (error_code & FAULT_OVER_TEMP) printf("Error: ADC Over Temperature\n");
	if (error_code & FAULT_CLOCK_ERROR) printf("Error: Master Clock Loss\n");

	// 3. Optional: Send alert to PC via USB
	uint8_t msg[] = "ADC_FAULT_STOPPED";
	CDC_Transmit_HS(msg, sizeof(msg));
}

void ADAR7251_Recover_From_Fault(void) {
	// Once physical issue is resolved, clear the latch
	ADAR7251_SPI_Write(REG_ASIL_CLEAR, 0x01);

	printf("Fault Cleared. System ready for restart.\n");
}


void Process_Radar_Data(uint8_t *ptr) {
	// IMPORTANT: Because DMA changed RAM without the CPU knowing,
	// we must invalidate the cache for this area before reading.
	// cache already disabled in MPU so this line not required anymore
	// SCB_InvalidateDCache_by_Addr((uint32_t*)ptr, TOTAL_BUF_SIZE / 2);
	if (Check_PLL_Lock() == PLL_UNLOCKED) {
		// PLL is not locked! The 10GHz signal is unstable.

		// STOP everything to prevent bad data causing false detections
		HAL_TIM_Base_Stop(&htim12);
		HAL_PSSI_Abort_DMA(&hpssi);

		// Notify PC of the hardware failure
		uint8_t err_msg[] = "ERR:PLL_LOCK_LOST";
		CDC_Transmit_HS(err_msg, sizeof(err_msg));

		return; // EXIT: Do not proceed to FFT
	}

	for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
		int base = i * 8;
		// ADAR7251 is Big-Endian: [High, Low]
		rx_raw[0][i] = (int16_t)((ptr[base + 0] << 8) | ptr[base + 1]);
		rx_raw[1][i] = (int16_t)((ptr[base + 2] << 8) | ptr[base + 3]);
		rx_raw[2][i] = (int16_t)((ptr[base + 4] << 8) | ptr[base + 5]);
		// ptr[base+6] and [base+7] is Channel 4 (Dummy) - ignore.
	}

	Process_Radar_Data_main();
}

void Transmit_Radar_Data(uint8_t *ptr, uint16_t len) {
	if (Check_PLL_Lock() == PLL_UNLOCKED) {
		// PLL is not locked! The 10GHz signal is unstable.

		// STOP everything to prevent bad data causing false detections
		HAL_TIM_Base_Stop(&htim12);
		HAL_PSSI_Abort_DMA(&hpssi);

		// Notify PC of the hardware failure
		uint8_t err_msg[] = "ERR:PLL_LOCK_LOST";
		CDC_Transmit_HS(err_msg, sizeof(err_msg));

		return; // EXIT: Do not proceed to FFT
	}


	uint8_t result = CDC_Transmit_HS(ptr, len);

	if (result == USBD_BUSY) {
		// Handle error: PC is not reading data fast enough
		// For 10GHz radar, you might need to increase your PC-side baud rate/app speed
	}

}

// TRIGGERED WHEN FIRST HALF (PING) IS FULL
void My_DMA_RxHalfCpltCallback(DMA_HandleTypeDef *hdma) {
	// Hardware is currently filling radar_stream_buf[4096 to 8191]
	// CPU processes radar_stream_buf[0 to 4095]
	Process_Radar_Data(&radar_stream_buf[0]);
	// if speed is faster than 70 mph, transfer bin result, raw phase to usb
	// Transmit_Radar_Data(&radar_stream_buf[0], TOTAL_BUF_SIZE / 2);
}

// TRIGGERED WHEN SECOND HALF (PONG) IS FULL
void My_DMA_PSSI_RxCpltCallback(DMA_HandleTypeDef *hdma) {
	// Hardware is currently filling radar_stream_buf[0 to 4095]
	// CPU processes radar_stream_buf[4096 to 8191]
	Process_Radar_Data(&radar_stream_buf[TOTAL_BUF_SIZE / 2]);
	// Transmit_Radar_Data(&radar_stream_buf[TOTAL_BUF_SIZE / 2], TOTAL_BUF_SIZE / 2);

}