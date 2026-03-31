
#include "adf4159.h"

#include "main.h"


void ADF4159_SPI_Write(uint32_t data) {
	// LE Pin Toggling: Unlike standard SPI peripherals where CS stays low for the whole transaction, the ADF4159 latches on the Rising Edge of LE. The STM32 hardware NSS pin often toggles automatically; for this chip, it is usually safer to control LE manually via HAL_GPIO_WritePin.
	HAL_GPIO_WritePin(SPI4_CS_ADF_GPIO_Port, SPI4_CS_ADF_Pin, GPIO_PIN_RESET);// CS Low
	HAL_Delay(1);
	HAL_SPI_Transmit(&hspi4, (uint8_t *)&data, 4, 100); // SPI is 8-bit, you send four bytes in a row.
	HAL_GPIO_WritePin(SPI4_CS_ADF_GPIO_Port, SPI4_CS_ADF_Pin, GPIO_PIN_SET);// CS High

	HAL_Delay(1);
}

void Init_ADF4159(void)
{
	// hardware reset
	HAL_GPIO_WritePin(ADF4159_CE_GPIO_Port, ADF4159_CE_Pin, GPIO_PIN_RESET);
	HAL_Delay(5);
	HAL_GPIO_WritePin(ADF4159_CE_GPIO_Port, ADF4159_CE_Pin, GPIO_PIN_SET);
	HAL_Delay(10); // Latching ADDR15 happens here
	// write regs
	for(int i = 0; i < 11; i++) {
		ADF4159_SPI_Write(adf4159_init_regs[i]);
	}
}


PLL_Status_t Check_PLL_Lock(void) {
	// Read the MUXOUT pin
	if (HAL_GPIO_ReadPin(ADF4159_MUX_GPIO_Port, ADF4159_MUX_Pin) == GPIO_PIN_SET) {
		return PLL_LOCKED;
	} else {
		return PLL_UNLOCKED;
	}
}