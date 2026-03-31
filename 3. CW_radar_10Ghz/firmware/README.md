
---
## SPI Configuration

Two separate SPI buses are required because the ADAR7251 and ADF4159 use incompatible SPI modes:

| Device | SPI Mode | CPOL | CPHA | Reason |
|---|---|---|---|---|
| ADAR7251 | **Mode 3** | 1 | 1 | Data is stable well before the rising clock edge — important for long traces on radar boards |
| ADF4159 | Mode 0 | 0 | 0 | Standard Analog Devices PLL SPI |

**ADAR7251 ADDR15 (Pin 32):** This pin is shared with DOUT6. It must be tied to GND (Address 0) or VIO (Address 1) through a **10 kΩ pull resistor**. The chip latches the address at power-up. The STM32 PSSI bus must be in **high-impedance state** during the power-up address latch, before the PSSI is enabled.

---
## Firmware

### ADAR7251 Initialization Sequence

#### Step 1 — Hardware Reset & Clock

1. Pull `RESET/PWDN` (Pin 43) low for ≥ 15 ns, then release high.
2. Verify that the master clock on `MCLKI` (Pin 16) is stable. The ADAR7251 accepts **16–54 MHz** on this pin. (Note: the STM32 can generate this from its clock output peripheral.)
3. The `ADDR15` pin state is already latched from power-up (see above).

#### Step 2 — Configure via SPI

```c
// 1. Set SPI Mode 3: CPOL=1, CPHA=1

// 2. Disable CRC (enables standard 5-byte SPI)
ADAR7251_WriteReg(0xFD00, 0x0000);

// 3. Configure PLL dividers based on MCLKI frequency
ADAR7251_WriteReg(0x0001, N_DIV);   // Integer divider
ADAR7251_WriteReg(0x0002, M_DIV);   // Fractional divider
ADAR7251_WriteReg(0x0003, PLL_CFG); // PLL config

// 4. Wait for PLL lock (Reg 0x005, Bit 0 = 1)
while ((ADAR7251_ReadReg(0x0005) & 0x0001) == 0);

// 5. Enable master logic
ADAR7251_WriteReg(0x0040, 0x0001); // MASTER_ENABLE

// 6. Enable all 4 ADC channels
ADAR7251_WriteReg(0x0041, 0x000F); // ADC_ENABLE

// 7. Enable internal clock generator
ADAR7251_WriteReg(0x0042, 0x0001); // CLKGEN_EN
```

#### Step 3 — Configure PSSI Interface (8-bit Parallel)

```c
// Switch from serial/nibble to parallel mode
ADAR7251_WriteReg(0x01C2, 0x0001); // OUTPUT_MODE: Bit0=1 → Parallel (PPI)

// Set 8-bit byte-wide output
// Bit2=1: Byte mode (8-bit), Bit1=0: High byte first, Bit0=0: 4 channels
ADAR7251_WriteReg(0x01C1, 0x0004); // PARALLEL_MODE (4 channels, byte-wide)

// Enable overload detection for data integrity
ADAR7251_WriteReg(0x0301, 0x0004); // Bit2=1: PDETECT_EN
```

> **Note on `PARALLEL_MODE` register:** Set `Bit0 = 1` for 2-channel output or `Bit0 = 0` for 4-channel output. For a 3-antenna setup, use **4-channel mode** (`Bit0 = 0`) — the 4th channel can be used for a reference or left inactive.

#### Step 4 — Synchronised Data Capture

In 8-bit PPI mode, `FS_ADC` (Pin 33) is **reassigned as Data Bit 7** and cannot be used for synchronisation. Use the `DATA_READY` / `DE` pin handshake instead:

```
STM32 Timer ──→ CONV_START (Pin 35, active-low pulse)
                    │
                    ↓ ADAR7251 starts conversion
                ADAR7251 asserts DATA_READY (Pin 22) HIGH
                    │
                    └──→ STM32 PSSI_DE pin goes HIGH
                              │
                              ↓ PSSI DMA captures 8-bit bus
                         DOUT0–DOUT7 (Pins 25–33)
                         [CH1_Hi][CH1_Lo][CH2_Hi][CH2_Lo]...
```

---

### Memory Architecture

The STM32H723 has a split memory topology across power domains. DMA and cache coherency must be managed carefully.

```
ADAR7251 PSSI bus
        │
        ↓ DMA1 (D2 domain) — crosses D2→D1 bridge
AXI SRAM  0x24000000  (D1 domain, 512 KB)
   ← Configure MPU: Non-Cacheable for this region →
        │
        ↓ USB DMA (D2 domain, but can reach AXI SRAM via interconnect)
USB HS (OTG_HS) → USB3320 PHY → USB cable → Host PC
```

**Memory placement rules:**

| Buffer | Location | Address | Reason |
|---|---|---|---|
| PSSI raw capture buffer | AXI SRAM | `0x24000000` | DMA1 accessible; large enough for full frame |
| USB control/descriptor | SRAM1 / SRAM2 | `0x30000000` | Close to USB core (D2 domain) |
| Stack / local variables | DTCM | `0x20000000` | Fastest possible CPU access (zero-wait-state) |

> ⚠️ **Cache coherency:** The H7 aggressively caches AXI SRAM. CPU reads/writes to this region will use the cache — DMA writes will not flush the cache. You **must** configure the MPU to mark the PSSI buffer region as **Non-Cacheable** to ensure DMA and CPU see the same data.

```c
// In usb_device.c — force USB handle into D2 SRAM
USBD_HandleTypeDef hUsbDeviceHS __attribute__((section(".RAM_D2")));
```

Add corresponding `SECTIONS` entries to the linker script (`.ld`) and tag all DMA-accessible buffers with `__attribute__((section(".AXI_SRAM")))`.

---

### DSP Pipeline

The full processing chain on the STM32 for one radar frame:

```
Raw I/Q samples (ADAR7251, 16-bit, 3 channels)
        │
        ↓ FMAC (hardware FIR accelerator)
   High-pass FIR (cutoff ~150 Hz) — removes DC offset and 1/f clutter
   Low-pass  FIR (cutoff ~5–6 kHz) — removes noise above max Doppler
        │
        ↓ arm_rfft_fast_f32 (CMSIS-DSP, hardware FPU)
   FFT per channel (e.g., 1024-point)
        │
        ↓ arm_cmplx_mag_f32 (SIMD — 2 values per clock)
   Magnitude spectrum per channel
        │
        ↓ CA-CFAR (Constant False Alarm Rate)
   Dynamic threshold = mean(guard cells) × α
   Detect peaks above threshold
        │
        ├──→ Doppler peak bin → speed (+ geometric correction by sin α)
        ├──→ Micro-Doppler hump width → Vmax, Vmin → spin rate (RPM)
        │
        ↓ CORDIC (hardware phase engine, HAL_CORDIC_Calculate_DMA)
   Phase θ = CORDIC(I, Q) per RX channel at detected Doppler bin
   (~15 clock cycles vs ~80 for software atan2f)
        │
        ↓
   ΔAoA = θ_RX2 − θ_RX1, θ_RX3 − θ_RX1
        │
        ↓
   Spin axis vector (geometry from ΔAoA between Doppler top/bottom points)
        │
        ↓ USB HS bulk transfer
   Host PC (JSON / binary frame)
```

**FMAC filter design note:** Standard software FIR on the Cortex-M7 costs ~100 instructions per sample for a 100-tap filter. The FMAC hardware executes the same filter in the background using Q1.15 fixed-point arithmetic. Call `HAL_FMAC_FilterStart()` and let the hardware run while the CPU proceeds to the next task.

**CORDIC normalisation:** Before feeding I and Q into the CORDIC, normalise the vector to unit magnitude (i.e., scale so that $\sqrt{I^2+Q^2} = 1$). This ensures the Q1.31 fixed-point input does not overflow and gives maximum angular resolution.

**CA-CFAR implementation:** The noise floor in X-band radar varies with environment (rain, multipath, ground clutter). A flat threshold fails in these conditions. CA-CFAR calculates the average noise power in a window of cells surrounding each test cell and sets the threshold as a multiple of that average. This provides a statistically constant false alarm rate regardless of the local noise level.

---
