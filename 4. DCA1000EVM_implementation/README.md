# AWR1843BOOST Raw IQ Capture via LVDS → Artix-7 FPGA

> **TL;DR** — The AWR1843BOOST out-of-the-box only streams processed radar *outputs* (detected objects: range, velocity, angle). To capture raw ADC I/Q samples you must tap the LVDS lines and feed them into a second board capable of high-speed serial capture. This project does that with a Xilinx Artix-7 FPGA. Disclaimer: currently I do not have AWR1843BOOST, the majority of the work is based on assumptions and search results, only provides a framework and thought process. To be tested with actual radar.

---
## Why You Need a Second Board

The AWR1843BOOST has **two output paths**:

| Interface | What it carries | Usable without extra hardware? |
|---|---|---|
| **UART / USB (XDS110)** | mmWave SDK processed data — detected objects with *range, velocity, azimuth angle, SNR* | ✅ Yes — plug in USB, run mmWave Demo |
| **LVDS (J6 connector)** | Raw ADC I/Q samples — 12-bit or 16-bit complex baseband, straight off the receive chain | ❌ No — requires a capture board |

The on-chip DSP (C674x + HWA) runs the radar signal processing pipeline and spits out **point clouds** over UART at a comfortable ~921600 baud. That's everything most mmWave demos need.

**Raw I/Q is different.** It is the digitized baseband signal *before* any FFT, CFAR, or beamforming. You need it for:

- Custom waveform processing / algorithm research
- MIMO virtual aperture synthesis
- Gesture / micro-Doppler analysis
- Machine learning on raw radar tensors
- Replicating or replacing TI's HWA processing in your own pipeline

TI sells the **DCA1000EVM** capture card for exactly this purpose. This project replaces the DCA1000EVM with a **direct LVDS connection to an Artix-7 FPGA**, eliminating the extra board cost and the Ethernet bottleneck.

---

## What the AWR1843 Actually Exports

### Over UART/USB (processed)

```
Frame N
└── Point Cloud
    ├── Object 0: range=3.2m  velocity=-0.4m/s  azimuth=+12°  SNR=18dB
    ├── Object 1: range=5.7m  velocity=0.0m/s   azimuth=-3°   SNR=22dB
    └── ...
```

- **Protocol**: TLV (Type-Length-Value) over UART
- **Rate**: One frame payload every frame period (e.g. 50 ms for 20 Hz)
- **Bandwidth**: << 1 Mbps, trivially handled by the USB-UART bridge

### Over LVDS (raw ADC)

The AWR1843 is a **3TX / 4RX** device. All 4 receive chains are captured over the 4 LVDS data lanes — one lane per RX, with I and Q interleaved word-by-word on that lane:

```
Chirp N (one burst per chirp)
└── All 4 RX lanes, simultaneously, DDR serialized
    ├── Lane 0: [0xF800 header][RX0_I₀][RX0_Q₀][RX0_I₁][RX0_Q₁]...  ← RX0 I+Q interleaved
    ├── Lane 1: [0xF800 header][RX1_I₀][RX1_Q₀][RX1_I₁][RX1_Q₁]...  ← RX1 I+Q interleaved
    ├── Lane 2: [0xF800 header][RX2_I₀][RX2_Q₀][RX2_I₁][RX2_Q₁]...  ← RX2 I+Q interleaved
    └── Lane 3: [0xF800 header][RX3_I₀][RX3_Q₀][RX3_I₁][RX3_Q₁]...  ← RX3 I+Q interleaved
```

- **Protocol**: Source-synchronous LVDS, DDR, with a 0xF800 sync header per chirp
- **Clock**: Gated — only active during ADC sampling window
- **Sample depth**: 16-bit complex (I and Q interleaved on the same lane, consecutive words)
- **Lane assignment**: 1 lane per RX receiver — all 4 RX captured simultaneously

> 📝 **De-interleave note:** In your FPGA, each lane delivers alternating I/Q words: word 0 = I sample, word 1 = Q sample, word 2 = I sample, etc. Your `iq_deinterleave` module must toggle a 1-bit state flag per lane to separate them — not treat separate lanes as I vs Q.

---

## Hardware Connection

### What You Need

| Item | Notes |
|---|---|
| AWR1843BOOST | The radar board — **do not** use the XDS110 UART path for IQ |
| Artix-7 FPGA board | Must have an HR I/O bank that can be powered at **1.8V** |
| Short PCB-to-PCB cable or direct header | Keep LVDS traces < 10 cm; matched-length pairs |
| 100 Ω differential termination | Built-in if you enable `DIFF_TERM=TRUE` in Artix XDC |

### J6 Connector Pinout (AWR1843BOOST)

```
J6 (2×10, 1.27mm pitch)
┌──────────────────────────────────────────┐
│  1  LVDS_CLK+                            │  ─── Source-synchronous clock (gated)
│  2  LVDS_CLK-                            │
│  3  LVDS_D0+  (RX0: I+Q interleaved)     │  ─── Lane 0
│  4  LVDS_D0-                             │
│  5  LVDS_D1+  (RX1: I+Q interleaved)     │  ─── Lane 1
│  6  LVDS_D1-                             │
│  7  LVDS_D2+  (RX2: I+Q interleaved)     │  ─── Lane 2
│  8  LVDS_D2-                             │
│  9  LVDS_D3+  (RX3: I+Q interleaved)     │  ─── Lane 3
│ 10  LVDS_D3-                             │
│ 11  GND                                  │
│ 12  GND                                  │
│ ... (power/GPIO)                         │
└──────────────────────────────────────────┘
```

> to be verified

### Signal Routing Rules

```
AWR1843BOOST J6                        Artix-7 HR Bank (1.8V VCCO)
──────────────────────────────────────────────────────────────────
LVDS_CLK+  ────────────────────────►  IBUFDS → BUFIO → ISERDESE2.CLK → BUFR → CLKDIV 
LVDS_CLK-  ────────────────────────►                              
                                                                  
LVDS_D0+   ────────────────────────►  IBUFDS → ISERDESE2 (Lane 0) 
LVDS_D0-   ────────────────────────►            
LVDS_D1+   ────────────────────────►  IBUFDS → ISERDESE2 (Lane 1)
LVDS_D1-   ────────────────────────►
LVDS_D2+   ────────────────────────►  IBUFDS → ISERDESE2 (Lane 2)
LVDS_D2-   ────────────────────────►
LVDS_D3+   ────────────────────────►  IBUFDS → ISERDESE2 (Lane 3)
LVDS_D3-   ────────────────────────►
GND        ────────────────────────►  GND (shared reference)
```

**Critical routing rules:**

- All 5 differential pairs (1 clock + 4 data) must be **length-matched** within ±5 mil
- Keep pairs in the **same I/O column** on the Artix so BUFIO can reach all ISERDESE2
- Do NOT route through vias between LVDS driver and FPGA input if avoidable
- Add 100 Ω series resistors on the AWR1843 side if your trace is > 10 cm

---

## LVDS Data Rate — All 4 RX

### Deriving the numbers

The LVDS bit clock is derived directly from the ADC sample rate. With **4 RX and complex data**, each of the 4 lanes carries **both I and Q** interleaved — so the effective per-RX data rate is double the sample rate:

```
LVDS bit clock    = adcSampleRate × 2       (DDR: one bit per clock edge)
LVDS word rate    = LVDS bit clock / 16      (16-bit words per lane)
Words per lane    = I + Q interleaved → 2 words per complex sample
Effective samples = word rate / 2            (I/Q pairs per second)
Total throughput  = 4 lanes × 16 bits × word_rate
```

### Table: Common adcSampleRate configurations

| adcSampleRate | LVDS Bit Clock | Per-lane word rate | **Total (4 lanes)** | Aggregate |
|---|---|---|---|---|
| 12.5 MSps | 25 MHz | 1.5625 MW/s | 6.25 MW/s | **200 Mbps** |
| 25 MSps | 50 MHz | 3.125 MW/s | 12.5 MW/s | **400 Mbps** |
| **37.5 MSps** | **75 MHz** | **4.6875 MW/s** | **18.75 MW/s** | **600 Mbps** |
| 50 MSps | 100 MHz | 6.25 MW/s | 25 MW/s | **800 Mbps** |
| 56.25 MSps *(max)* | 112.5 MHz | 7.03 MW/s | 28.125 MW/s | **900 Mbps** |

> The AWR1843 maximum ADC sample rate is **56.25 MSps**, giving a maximum aggregate LVDS throughput of **~900 Mbps** across all 4 lanes. This is well within Artix-7 LVDS capability (up to ~1.25 Gbps per DDR pin pair on HR banks).

### Per-chirp data volume

```
bytes_per_chirp = numAdcSamples × numRX × 2 samples/complex × 2 bytes/sample
               = 256 × 4 RX × 2 (I+Q) × 2 bytes
               = 4096 bytes  (typical config, all 4 RX, complex)

bytes_per_frame = bytes_per_chirp × numChirpsPerFrame
               = 4096 × 128
               = 524288 bytes = 512 KB per radar frame
```

At 20 frames/sec → **~10.5 MB/s** sustained to your FIFO / memory.

---

## Design Considerations

### 1. Voltage Compatibility

The AWR1843 LVDS I/O is **1.8V**. Artix-7 HR banks support 1.8V VCCO natively. HP banks also support 1.8V but use a different `IOSTANDARD` (`LVDS` instead of `LVDS_25`).

> **Never connect AWR1843 LVDS lines to a 3.3V-powered bank.** The inputs will not be damaged (they are 3.3V tolerant as inputs) but the differential threshold levels will be wrong and you will get corrupted data.

### 2. Clock Domain Isolation

There are **three clock domains** in this design:

| Domain | Source | Frequency | Notes |
|---|---|---|---|
| `lvds_clk` (BUFIO) | AWR1843 LVDS clock | 25–112.5 MHz | I/O column only, feeds ISERDESE2 |
| `lvds_div_clk` (BUFR) | BUFIO ÷ 8 | 3–14 MHz | Fabric logic, sync state machines |
| `user_clk` | On-board oscillator | 100–200 MHz | Your application logic |

All crossings between `lvds_div_clk` and `user_clk` must use proper CDC (async FIFO with Gray-coded pointers, or 2-FF synchronizers for single-bit flags).

### 3. Bit Slip Calibration

ISERDESE2 deserializes bits but does not know where word boundaries are. At startup:

1. Issue `BITSLIP` pulses (minimum 2 CLKDIV cycles apart — **hardware enforced**)
2. After each slip, check the deserialized 16-bit value against `0xF800`
3. Keep slipping until the pattern aligns — worst case 8 slips for 8:1 ratio
4. Confirm across multiple chirp headers before asserting `sync_locked`

> ISERDESE2 in `NETWORKING` mode requires **exactly 2 CLKDIV cycles between bitslip pulses** — violating this causes a metastability glitch that can permanently corrupt the deserializer state until reset. Guard this carefully in your controller.

### 4. Clock-Gated Frame Boundaries

The AWR1843 **gates the LVDS clock off** between chirps. This is intentional:

- Clock **high** → ADC sampling active, data flowing
- Clock **idle** (held static) → inter-chirp guard time

Your FPGA must detect clock presence/absence to find chirp boundaries. Use a free-running divided clock to sample the LVDS clock line and count cycles since the last edge toggle. Do not rely on external GPIO for framing.

### 5. LVDS I/O Column Placement 

On Artix-7, `BUFIO` can only drive ISERDESE2 instances in the **same I/O bank column**. If your clock pair and data pairs span different columns, BUFIO cannot reach across — and Vivado will error out. Plan your pinout so that all 5 differential pairs land in the same left or right I/O column.

### 6. Data Storage Bandwidth

With all 4 RX active and complex data, a typical frame (256 samples × 128 chirps × 4 RX × 4 bytes/complex sample) is **512 KB**. At 20 fps that's ~10 MB/s — manageable with BRAM double-buffering. At max sample rate (56.25 MSps) with short frame periods, sustained throughput can approach **~360 MB/s** — plan your DDR controller accordingly.

| Scenario | Aggregate LVDS | Frame size (256 samp, 128 chirp, 4 RX) | @ 50 fps |
|---|---|---|---|
| Low rate (12.5 MSps) | 200 Mbps | 512 KB | 25 MB/s |
| Typical (37.5 MSps) | 600 Mbps | 512 KB | 25 MB/s |
| Max rate (56.25 MSps) | 900 Mbps | 512 KB | 25 MB/s |
| Max rate + short frame | 900 Mbps | depends | up to **~360 MB/s** |

### 7. mmWave SDK Configuration Lock-In

Your FPGA RTL must match the mmWave chirp profile **exactly**:

```
numAdcSamples    → SAMPLES_PER_CHIRP parameter
adcSampleRate    → LVDS bit clock, BUFR divide, ISERDESE2 DATA_WIDTH
numRxAnt         → NUM_LANES (1 lane per active RX; AWR1843 = 4 lanes for 4 RX)
dataFormat       → Complex: I+Q interleaved per lane | Real: I only per lane
lvdsLaneFmt      → lane-to-RX mapping (Lane N = RX N)
```

If you change the mmWave profile without updating the FPGA parameters, you will silently capture garbage data with no obvious error flag.

---

## Quick-Start Checklist

- [ ] Set Artix-7 bank VCCO to **1.8V**
- [ ] Use `IOSTANDARD = LVDS_25` (HR bank) or `LVDS` (HP bank) in XDC
- [ ] Enable `DIFF_TERM = TRUE` on all LVDS inputs (100 Ω internal termination)
- [ ] Route all 5 differential pairs to the **same I/O column**
- [ ] Length-match all pairs to within **±5 mil** on your PCB
- [ ] Set `create_clock -period` in XDC to match your `adcSampleRate × 2`
- [ ] Set `SAMPLES_PER_CHIRP` parameter to match `numAdcSamples` in mmWave config
- [ ] Enforce **2-cycle bitslip cooldown** in bit_sync state machine
- [ ] Declare `set_false_path` between `lvds_div_clk` and `user_clk`
- [ ] Verify `bit_sync_locked` AND `word_sync_locked` before trusting IQ output

---
