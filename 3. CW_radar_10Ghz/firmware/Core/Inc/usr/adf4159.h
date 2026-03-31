///////////////////////////////////////////////////////////////////////////////
//
// Applications Information from the ADF4159 Rev. E Data Sheet
//
// ADF4159 Initialization Sequence:
// 1. Program Delay Register (R7)
// 2. Program Step Register (R6)
//      a. Load the Step Register TWICE
//      b. First with STEP_SELECT_WORD_1 (STEP SELECT = 0)
//      c. And then with STEP_SELECT_WORD_2 (STEP SELECT = 1)
// 3. Program Deviation Register (R5)
//      a. Load the Deviation Register TWICE
//      b. First with DEVIATION_SELECT_WORD_1 (DEVIATION SELECT = 0)
//      c. And then with DEVIATION_SELECT_WORD_2 (DEVIATION SELECT = 1)
// 4. Program Clock Register (R4)
//      a. Load the Clock Register TWICE
//      b. First with CLK_DIV_1_SEL (CLOCK DIVIDER SELECT = 0)
//      c. And then with CLK_DIV_2_SEL (CLOCK DIVIDER SELECT = 1)
// 5. Program Function Register (R3)
// 6. Program R Divider Register (R2)
// 7. Program LSB FRAC Register (R1)
// 8. Program FRAC-INT Register (R0)
//
///////////////////////////////////////////////////////////////////////////////
///
#ifndef _DT_BINDINGS_IIO_FREQUENCY_ADF4159_H_
#define _DT_BINDINGS_IIO_FREQUENCY_ADF4159_H_

#include <stdint.h>
#include "stm32h7xx_hal_spi.h"


extern SPI_HandleTypeDef hspi4;   // For Control (Registers)


typedef enum {
	PLL_UNLOCKED = 0,
	PLL_LOCKED = 1
} PLL_Status_t;


// Single ramp-out with fast triangle ramp
// Array based on calculations above for 10GHz (5GHz at RF_IN)
uint32_t adf4159_init_regs[] = {
	0x00000007, // R7: Delay, Delay Reg: All ramps/delays disabled.
	0x00000006, // R6: Step (SEL 0) Step Reg: Step Select 0, Step Word 0.
	0x00800006, // R6: Step (SEL 1) Step Reg: Step Select 1, Step Word 0.
	0x00000005, // R5: Deviation (SEL 0) Deviation Reg: Dev Select 0, Dev Word 0.
	0x00800005, // R5: Deviation (SEL 1) Deviation Reg: Dev Select 1, Dev Word 0.
	0x00000004, // R4: Clock (SEL 0) Clock Reg: Div 1, Clock Divider Off.
	0x00000044, // R4: Clock (SEL 1) Clock Reg: Div 2, Clock Divider Off.
	// R2 R3 ok, others to be verified
	0x00000443, // R3: Function (Positive Polarity, SINGLE RAMP BURST)
	0x07008002, // R2: R=1, R_divider=0, D=0, CP=2.5mA, Prescaler=8/9 for f>8Ghz, CLK1 Divider Value=2
	0x0AAA8001, // R1: LSB FRAC = 0x1555 (Shifted to DB[27:15]).no phase shift
	0x38341550  // R0: INT=50, FRAC MSB=0x2AA, RAMP=OFF, MUXOUT=Digital Lock Detect
};

PLL_Status_t Check_PLL_Lock(void);
void Init_ADF4159(void);

#endif /* _DT_BINDINGS_IIO_FREQUENCY_ADF4159_H_ */