# K-LC7 24 GHz Radar — Limitations & Signal Processing Analysis

> A practical breakdown of why the RFbeam K-LC7 is fundamentally
> slow for high-speed targets, with worked signal chain numbers
> and architecture trade-offs for a toy FMCW/CW project.

---

## Hardware Overview

| Parameter | Value |
|---|---|
| Frequency | 24.0 – 24.25 GHz ISM band |
| Architecture | 1TX, 2RX, integrated VCO |
| IF outputs | I1, Q1 (Rx1) and I2, Q2 (Rx2) |
| Tuning range | 200 – 350 MHz (typ 250 MHz) |
| VCO sensitivity | 80 MHz/V |
| Rx spacing | 8.763 mm |
| IF bandwidth | 0 – 50 MHz |
| IF output impedance | 50 Ω |

---

## The Core Problem: VCO Modulation Bandwidth

This is the single most important limitation and it constrains
everything downstream.

### What the datasheet says

```
VCO Modulation Bandwidth:  BVCO = 100 kHz  (at Δf = 20 MHz)
```

This is the −3 dB bandwidth of the VCO's FM response. It means
the VCO output frequency **cannot track a tuning voltage that
changes faster than 100 kHz**. Any modulation signal above
100 kHz is attenuated and phase-shifted — the VCO simply cannot
follow it.

### Practical consequence: minimum chirp time

Each frequency step requires the VCO to settle. The settling
time per step is at minimum:

```
t_step = 1 / BVCO = 1 / 100 kHz = 10 µs per step
```

### Voltage and step calculation

VCO sensitivity is 80 MHz/V, but this is a **centre-band
approximation**. The relationship becomes nonlinear near the
band edges (24.000 GHz and 24.250 GHz) as the VCO approaches
its push-pull limits. The usable linear region is roughly the
centre 60% of the tuning range.

To sweep 20 MHz of bandwidth:

```
ΔV_required = Δf / SVCO = 20 MHz / 80 MHz·V⁻¹ = 0.25 V total swing
```

> Correction: 0.25 V is the **total voltage swing** for the
> entire 20 MHz sweep, not 0.25 V per step. Each individual step
> voltage depends on how many steps you divide this into.

To cover the full 0–5 V tuning range (≈ 400 MHz total at
80 MHz/V) in discrete steps:

```
For 20 equal steps across 0–5 V:
  Step size = 5 V / 20 = 0.25 V per step
  Frequency jump per step = 0.25 V × 80 MHz/V = 20 MHz per step

This means each step covers the entire target bandwidth in one
jump — not useful for a smooth chirp.
```

For a useful chirp with N_steps steps across 250 MHz bandwidth:

```
Minimum N_steps for smooth ramp (subjective, ≥ 10 recommended):
  N = 20 steps → step size = 250 MHz / 20 = 12.5 MHz/step
  N = 100 steps → step size = 2.5 MHz/step (smoother)

Minimum chirp time at N = 20 steps:
  T_chirp_min = N × t_step = 20 × 10 µs = 200 µs

At N = 100 steps:
  T_chirp_min = 100 × 10 µs = 1000 µs = 1 ms
```

The 200 µs minimum chirp time at only 20 steps is the
**hard floor** imposed by the modulation bandwidth. You cannot
make the chirp faster without the VCO failing to track.

---

## Maximum Unambiguous Velocity

### Formula

```
Vmax = λ / (4 × Tc)

Where:
  λ  = c / fTX = 3×10⁸ / 24.125×10⁹ = 12.43 mm
  Tc = chirp repetition interval (chirp time + idle/settle time)
```

> Formula confirmed correct for the two-chirp symmetric
> FMCW case. The factor of 4 (rather than 2) accounts for the
> fact that you need two chirps spaced Tc apart to resolve
> velocity, giving an effective observation window of 2Tc.

### With minimum chirp time (no settle time, ideal)

```
Tc = 200 µs
Vmax = 0.01243 / (4 × 200×10⁻⁶) = 0.01243 / 8×10⁻⁴ = 15.5 m/s = 34.7 mph
```

### With realistic settle time

The K-LC7 VCO needs time to settle after the ramp before the
next chirp begins. A conservative estimate is 50 µs settle time:

```
Tc = 200 µs chirp + 50 µs settle = 250 µs total

Vmax = 0.01243 / (4 × 250×10⁻⁶) = 12.43 m/s ≈ 27.8 mph
```


### Comparison with the original 77.7 m/s figure

```
The 77.7 m/s figure uses Tc = 40 µs:
  Vmax = 0.01243 / (4 × 40 µs) = 77.7 m/s = 173 mph ✓

This would require a chirp time of 40 µs — impossible on the
K-LC7 because BVCO = 100 kHz forces t_step ≥ 10 µs, and you
need at minimum 4–20 steps per chirp.

173 mph is the theoretical ceiling if the modulation bandwidth
were not a constraint. In practice the K-LC7 is limited to
~28 mph with a functional chirp.
```

---

## Velocity Resolution

### Formula

```
Vres = λ / (2 × Tf)

Where Tf = N_chirps × Tc = total frame duration

> The correct result for N=128 chirps at Tc=200 µs:

Tf = 128 × 200 µs = 25.6 ms

Vres = 0.01243 / (2 × 0.0256) = 0.243 m/s

Including 50 µs settle time:
Tf = 128 × 250 µs = 32 ms
Vres = 0.01243 / (2 × 0.032) = 0.194 m/s
```

---

## Frame Rate

```
With Tc = 200 µs (no settle):
  Frames/sec = 1 / (128 × 200 µs) = 1 / 25.6 ms = 39 fps ✅

With Tc = 250 µs (50 µs settle):
  Frames/sec = 1 / (128 × 250 µs) = 1 / 32 ms = 31 fps ✅
```

> Both figures confirmed correct.

---

## Angular Field of View and Resolution

### Maximum unambiguous angle

```
θ_max = ±arcsin(λ / (2d))
       = ±arcsin(12.43 mm / (2 × 8.763 mm))
       = ±arcsin(0.709)
       = ±45.2°
```

The ±40° figure in the datasheet represents the practical
limit where sidelobe interference starts to degrade
measurement quality, not the strict mathematical ambiguity
limit.

### Angular resolution with 2 RX antennas

```
With N_ant = 2 receiving antennas:
  θ_res ≈ 2 / N_ant = 1 radian = 57.3°
```

This means two objects must be separated by more than 57°
to be resolved as distinct targets. With only 2 RX elements
the K-LC7 effectively provides a binary left/right
discrimination rather than fine angular resolution.


### Phase difference equation

```
Δφ = (2π × d × sin(θ)) / λ  
```

---

## Range Resolution

```
d_res = c / (2B)

For B = 250 MHz (full tuning range):
  d_res = 3×10⁸ / (2 × 250×10⁶) = 0.6 m  

For B = 20 MHz (limited sweep in this project):
  d_res = 3×10⁸ / (2 × 20×10⁶) = 7.5 m  
```

> The 7.5 m range resolution with only 20 MHz sweep makes
> the K-LC7 essentially a presence detector rather than a
> ranging sensor when constrained by modulation bandwidth.
> The full 250 MHz gives 0.6 m which is workable.

---

## ADC Sampling Rate Requirement

### Beat frequency for maximum range

For a target at range R_max, the beat frequency is:

```
fb = (2 × R_max × S) / c

Where S = chirp slope = B / T_chirp

For B = 250 MHz, T_chirp = 200 µs:
  S = 250×10⁶ / 200×10⁻⁶ = 1.25×10¹² Hz/s 

For R_max = 10 m:
  fb = (2 × 10 × 1.25×10¹²) / (3×10⁸)
     = 2.5×10¹³ / 3×10⁸
     = 83.3 kHz 

Nyquist requirement:
  f_ADC ≥ 2 × fb_max = 166.7 kHz minimum for 10 m range
```

### Actual ADC setup

```
Clock: 27 MHz / (15 + 3 cycles) / 2 channels = 750 kSPS total

At 375 kSPS per channel with 200 µs chirp:
  Samples per chirp = 375,000 × 200×10⁻⁶ = 75 samples

At 750 kSPS per channel:
  Samples per chirp = 750,000 × 200×10⁻⁶ = 150 samples → 128 FFT ✅
```

> The 150 samples per chirp fits into a 128-point FFT
> (discard the first few samples during VCO settle) or
> zero-pad to 256 for better frequency resolution.

---

## High Speed Target Analysis: Golf Ball / Club Head

```
Target speed: 140 mph = 62.6 m/s
Distance: ~4 m (driver) to ~3 m (iron)
Time at target range: 4 m / 62.6 m/s ≈ 64 ms

Frames available at 31 fps: 64 ms × 31 = ~2 frames ✅

This is marginal — you get at most 2 complete range-Doppler
frames on the target before it is out of range.
```

### Velocity aliasing at 140 mph

```
Vmax ≈ 28 mph with 250 µs Tc
Target velocity = 140 mph

Aliased velocity = 140 mod (2 × 28) = 140 mod 56 = 28 mph
```

The measured velocity will alias badly. However:

> **Angle estimation is independent of velocity aliasing.**
> The phase difference between Rx1 and Rx2 is computed from
> the complex peak value in the Range-Doppler map. The peak
> exists at the correct range bin regardless of velocity
> aliasing. The angle formula uses only the spatial phase
> difference, not the temporal (Doppler) phase, so it
> remains valid even when velocity is wrapped.

The caveat is also correct: if the target SNR is too low
due to high velocity (range migration across bins during
the chirp), the peak may be smeared and the angle estimate
noisy.

---

## Angle Estimation Signal Processing Pipeline


```
Step 1:  Range FFT on I+jQ for each Rx independently
         → Rx1_range[k] and Rx2_range[k]

Step 2:  Doppler FFT across N chirps for each range bin
         → Range-Doppler map per Rx

Step 3:  Find peak at (range_bin, doppler_bin) = target

Step 4:  Extract complex values at the peak:
         Z1 = I1 + jQ1   (from Rx1 map at target bin)
         Z2 = I2 + jQ2   (from Rx2 map at target bin)

Step 5:  Phase of each:
         φ1 = atan2(Q1, I1)
         φ2 = atan2(Q2, I2)

Step 6:  Phase difference with wrapping:
         Δφ = angle(Z2 × conj(Z1))   ← cleaner than subtraction,
                                         handles wrapping automatically

Step 7:  Angle of arrival:
         θ = arcsin((Δφ × λ) / (2π × d))
```

> The pipeline is correct. One implementation note:
> `angle(Z2 × conj(Z1))` is more numerically robust than
> `atan2(Q2,I2) - atan2(Q1,I1)` because it handles the
> ±π wrapping in a single operation.

---

## DAC Rate Analysis

### For generating the VCO ramp

```
12-bit DAC: 2¹² = 4096 steps over 0–5 V
Step size: 5 V / 4096 = 1.22 mV
Frequency resolution: 1.22 mV × 80 MHz/V = 0.098 MHz = 97.7 kHz per LSB
```

For a 250 MHz sweep across N DAC steps:

```
Steps needed: 250 MHz / 0.098 MHz = 2560 DAC counts

DAC rate for T_chirp = 200 µs:
  f_DAC = 2560 / 200 µs = 12.8 MSPS
```

> This is faster than most integrated MCU DACs.
> STM32 DAC max is typically 1–2 MSPS.

For a coarser 20-step ramp (each step = 128 DAC counts):

```
20 steps in 200 µs:
  f_DAC = 20 / 200 µs = 100 kSPS  ← well within any MCU DAC
```

For 256 steps in 200 µs (smoother ramp):

```
f_DAC = 256 / 200 µs = 1.28 MSPS
```

> Correction: 256 steps / 200 µs = **1.28 MSPS**, not
> 1.23 MSPS (minor arithmetic error). Also the 5.12 MSPS
> figure in the original notes does not follow directly from
> these numbers — 5.12 MSPS would require 1024 steps in 200 µs.

**an external op-amp buffer
on the DAC output is needed** if you want a fast, low-impedance
drive into the VCO_In pin (120 kΩ internal impedance but
still has parasitic capacitance that limits slew rate at
high DAC update rates).

---

## ADC Data Rate and Bandwidth

```
Sample rate: 750 kSPS per channel
Bit depth: 16 bits
Channels: 4 (I1, Q1, I2, Q2)

Raw data rate = 4 × 750,000 × 16 bits = 48 Mbps = 6 MB/s ✅

Between-chirp data is useless (VCO settling):
  Useful data fraction = 200 µs / 250 µs = 80%
  Effective useful rate = 6 MB/s × 0.8 = 4.8 MB/s

MCU DMA bandwidth comparison:
  200 MHz Cortex-M4: ~20 MB/s DMA   → 4× headroom ✅
  400 MHz Cortex-M7: ~35 MB/s DMA   → 7× headroom ✅
```

### USB transfer options

```
USB 2.0 High Speed (480 Mbps):
  Theoretical: ~60 MB/s
  Practical bulk: ~37–42 MB/s
  
Raw data: 6 MB/s → USB 2.0 HS has 6× headroom ✅
Even at full 42 MB/s capacity this sensor is trivially handled.

USB Audio Class:
  Clever approach — the OS treats the device as a sound card,
  bypassing custom driver requirement entirely. Stereo 24-bit
  at 192 kSPS = 1.15 MB/s per channel. Sufficient for I/Q
  data at baseband rates. Latency is higher (~20 ms) but
  acceptable for most applications.
```

---

## FFT Processing Budget

```
256-point complex FFT on Cortex-M4/M7 (CMSIS-DSP):
  arm_cfft_f32(): ~50 µs at 200 MHz ✅
  arm_cfft_q15(): ~15 µs at 200 MHz (fixed-point, faster)

Per chirp budget:
  Chirp time: 200 µs
  ADC DMA fill: runs in background
  FFT computation: 50 µs (overlaps with next chirp DMA)

Per frame (128 chirps):
  Range FFT × 128 chirps × 2 channels = 256 FFTs
  = 256 × 50 µs = 12.8 ms at 200 MHz
  Frame time = 128 × 250 µs = 32 ms
  FFT headroom: 32 ms − 12.8 ms = 19.2 ms fits on-device

Doppler FFT (128-point across chirps):
  Additional 128-point FFT per range bin of interest
  If processing 10 range bins: 10 × 50 µs = 0.5 ms  ← trivial
```

> For a toy project, on-device processing is entirely feasible.
> Offloading to PC or FPGA only becomes necessary if you want
> real-time processing of all range bins simultaneously or
> very low latency.

---

## Summary of Limitations

| Limitation | Root Cause | Impact | Workaround |
|---|---|---|---|
| Slow chirp (min 200 µs) | BVCO = 100 kHz | Vmax ≈ 28 mph | None — hardware limit |
| Low Vmax | Slow chirp | Cannot track fast targets unambiguously | FSK mode instead of FMCW |
| 7.5 m range resolution | Only 20 MHz practical BW | Cannot separate close targets | Use full 250 MHz sweep |
| Poor angular resolution | Only 2 RX antennas | 57° minimum separation | Accept limitation |
| ±45° FoV | λ/2d antenna spacing | Cannot see wide angles | Mechanical scan |
| Nonlinear VCO | 24 GHz band edge | Chirp nonlinearity → range sidelobes | Stretch/DFT correction |
| I/Q imbalance ±3 dB | Datasheet spec | Mirror image in spectrum | Digital I/Q correction |
| 90° ±10° phase shift | Datasheet spec | Reduces image rejection to ~20 dB | Gram-Schmidt correction |
| 12 MSPS DAC needed | 2560 steps/chirp ideal | MCU DAC too slow for smooth ramp | Coarser 20–256 step ramp |
| Only 2 frames at 140 mph | Slow frame rate | Insufficient data for golf tracking | Wrong sensor for this app |

---

## When the K-LC7 is the Right Tool

```
 Walking speed detection (0–5 mph)         → Vmax adequate
 Presence detection (security, lighting)   → No range/vel needed
 Vital signs (breathing, heart rate)       → CW mode, not FMCW
 Slow vehicle speed (<30 mph)              → Just within Vmax
 Short-range parking/proximity (<5 m)      → Range resolution OK
 Learning/prototyping FMCW signal chain    → Cheap, integrated

 Golf ball/club head tracking (>100 mph)   → Vmax too low
 Fine angular imaging (<10°)               → Only 2 RX
 Long range (>20 m)                        → Sensitivity marginal
```

---

## Quick Reference: Key Numbers

```
λ at 24.125 GHz          = 12.43 mm
Rx spacing d             = 8.763 mm = 0.705λ
Modulation BW limit      = 100 kHz → 10 µs/step minimum
Minimum chirp time       = 200 µs (20 steps)
Typical chirp time       = 1 ms (100 steps, smoother)
Vmax at Tc=250 µs        = 12.4 m/s = 27.8 mph
Vres at N=128, Tc=250µs  = 0.194 m/s
Frame rate (128 chirps)  = 31 fps with settle time
Range resolution (250MHz)= 0.6 m
Range resolution (20MHz) = 7.5 m
Angular FoV              = ±45°
Angular resolution       = ±28.6° (57° between objects)
ADC requirement (10m)    = >167 kSPS
Typical ADC rate         = 375–750 kSPS
Raw data rate (4ch)      = 6 MB/s
USB 2.0 HS headroom      = 6×
```

---
