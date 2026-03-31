//
// Created by wzw on 10/28/25.
//

#ifndef DAC_H
#define DAC_H

// DAC Configuration for triangle wave chirp, to tune here
#define DAC_CENTER_VALUE  2048    // 2.5V for 12-bit DAC (4096 levels)
#define CHIRP_AMPLITUDE   410     // 1V swing = 4096/5 ≈ 410 codes
#define CHIRP_MIN         (DAC_CENTER_VALUE - CHIRP_AMPLITUDE/2)
#define CHIRP_MAX         (DAC_CENTER_VALUE + CHIRP_AMPLITUDE/2)

// Timer for chirp generation (determines chirp rate)
#define CHIRP_TIMER_FREQ  84000000  // 84 MHz
#define CHIRP_RATE        5000      // 5 kHz chirp rate = 1/200us
#define SAMPLES_PER_CHIRP 256       // Chirp resolution

void start_fmcw_chirp(void);

#endif //DAC_H
