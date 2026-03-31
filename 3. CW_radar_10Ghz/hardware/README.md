
---
## Mechanical Design

| Item | Value | Notes |
|---|---|---|
| Operating wavelength | 30 mm | at 10 GHz in free space |
| Required $\lambda/2$ spacing | 15 mm | for unambiguous interferometry |
| Effective $\lambda/2$ | 14.3 mm | slightly below free-space due to antenna size |
| Practical limit | >14.3 mm | horn antennas side-by-side cannot meet this |

Because strict $\lambda/2$ spacing is physically impractical with horn antennas, full MIMO interferometry is **not** used. The system instead relies on a **single ΔAoA baseline** between the top and bottom Doppler peaks of the ball, combined with camera/FMCW data for global orientation.

Horn-to-horn air isolation (side-by-side mounting) is assumed to be **≥ 40 dB**.

---

## Antenna — 10 GHz Horn (WR-90)

**Waveguide selection: WR-90** (22.86 mm × 11.43 mm)

WR-90 is the standard choice for lower X-band radar. WR-75 (19.05 mm × 9.525 mm) offers better performance at the high end of X-band but is harder to machine for DIY builds.

**WR-90 cutoff frequencies:**

| Mode | Cutoff (GHz) |
|---|---|
| TE10 (dominant) | 6.56 |
| TE20 | 13.11 |
| TE01 | 14.75 |
| TE11 / TM11 | 16.15 |

At 10 GHz only the TE10 mode propagates — the next mode (TE20) is well above the operating frequency, ensuring clean single-mode operation.

**Coaxial-to-waveguide probe feed:**

- **Feed type:** Side feed (probe through the wide wall, parallel to the narrow wall). This aligns the probe with the vertical E-field of the TE10 mode.
- **Probe insert depth:** $\approx \lambda_0 / 4 = 5\ \text{mm}$ (based on free-space wavelength $\lambda_0 = 30\ \text{mm}$ at 10 GHz).
- **Probe distance from back wall (short):** exactly $\lambda_g / 4 = 9.9\ \text{mm}$, where the **guide wavelength** in WR-90 at 10 GHz is:

$$\lambda_g = \frac{\lambda_0}{\sqrt{1 - (f_c/f)^2}} = \frac{30}{\sqrt{1-(6.56/10)^2}} \approx 39.7\ \text{mm}$$

---

## Processing — STM32H723VGT

The STM32H723VGT (Cortex-M7, 550 MHz) is chosen over an FPGA for this application because the required DSP tasks map directly onto its hardware accelerators, avoiding FPGA complexity and BGA routing:

| Accelerator | Task | Benefit |
|---|---|---|
| **FPU** (hardware) | FFT via CMSIS-DSP `arm_rfft_fast_f32` | Parallel floating-point pipeline |
| **CORDIC** | Phase / angle from I+Q data | ~15 clock cycles vs ~80 for `atan2f()` |
| **FMAC** | FIR/IIR filter execution | Background filtering, zero CPU overhead |
| **PSSI** | Parallel data capture from ADAR7251 | 8-bit byte-wide bus, DMA-driven |
| **USB HS** | Data streaming to host | Via USB3320 PHY |

---

## RF Chain

### TX Path

```
HMC1163 VCO  →  ÷2 coupler  →  Attenuator  →  ADF4159 PLL (feedback)
     |
     ↓ 11 dBm RF output
PMA3-14LN+ (gain = 21.6 dB, P1dB = 16.6 dBm, Vdd = 6V)
     |
     ↓ ~20 dBm (approaching saturation — use with caution; P1dB = 16.6 dBm)
15 dB 3-way Wilkinson splitter (–4.8 dB insertion + 7.5 dB split = –7 dB per port typical)
     |
     ├──→ TX horn antenna
     ├──→ LTC5548 mixer LO port (target: –2 dBm, within –6 to +6 dBm spec)
     └──→ (third port terminated / spare)
```

> ⚠️ **Note:** The PMA3-14LN+ P1dB is 16.6 dBm and full saturation occurs around 18–20 dBm. Operating the amplifier above P1dB introduces harmonic distortion and gain compression, which degrades the CW spectral purity. Consider adding a fixed attenuator after the VCO output stage or reduce VCO drive to keep the amplifier below P1dB.

**Key components:**

| Component | Parameter | Value |
|---|---|---|
| HMC1163 | Supply | 5 V |
| HMC1163 | Output power | +11 dBm |
| HMC1163 | RF/2 feedback to PLL | +6 dBm → attenuated to 0 dBm |
| ADF4159 | RF input level | 0 dBm |
| PMA3-14LN+ | Vdd | 6 V |
| PMA3-14LN+ | Max CW input | +12 dBm |
| PMA3-14LN+ | Gain at 10 GHz | 21.6 dB |
| LTC5548 | LO input range | –6 to +6 dBm |

---

### RX Path

```
Horn antenna (RX1 / RX2 / RX3)
     |
PMA3-14LN+ LNA (NF = 2.1 dB, gain = 21.6 dB)
     |
LTC5548 IQ mixer (RF in, LO from TX splitter)
     |
ADA4940 differential amplifier (noise = 3.9 nV/√Hz)
     |
ADAR7251 quad ADC (16-bit, 1–4 MSPS)
     |
STM32H723 (PSSI → DMA → AXI SRAM → USB HS)
```

**Noise floor of PMA3-14LN+ (referred to input):**

$$v_n = \sqrt{4 k T R \cdot 10^{NF/10}} = \sqrt{4 \times 1.38\times10^{-23} \times 290 \times 50 \times 10^{2.1/10}} \approx 1.15\ \text{nV}/\sqrt{\text{Hz}}$$

> **Note on units:** RF engineers express noise as a power ratio (NF in dB), while analog engineers express it as a voltage density (nV/√Hz). Both describe the same physical noise; the formula above converts between them assuming a 50 Ω system impedance.

---

## Power Supply & Voltage Domains

| Rail | Voltage | Supplied To |
|---|---|---|
| 3V3_A | 3.3 V | ADAR7251 analog supply |
| 1V8 | 1.8 V | STM32H723VGT I/O, ADAR7251 digital I/O (VIO), USB3320 VDDIO, ADF4159 digital supply |
| 3V3 | 3.3 V | ADF4159 analog supply |
| 6 V | 6 V | PMA3-14LN+ Vdd |
| 5 V | 5 V | HMC1163 Vcc |

The ADAR7251 operates at 3.3 V analog but its SPI and PSSI digital I/O run at 1.8 V (VIO). All logic signals between the ADAR7251 and STM32 must be level-shifted or the STM32 GPIO must be configured for 1.8 V (the H723 supports this natively via the VDDIO2 domain).

The USB3320 VDDIO pin must also be set to 1.8 V to match the STM32 PSSI bus voltage.

---

## Component Selection at 10 GHz

At 10 GHz, **standard passive components fail silently** because their parasitic inductance (ESL) and parasitic capacitance (ESR) cause them to self-resonate well below the operating frequency. Every component must be selected for an SRF comfortably above 10 GHz.

### General Rules

- **Package size:** Use 0402 throughout. 0201 has lower parasitic inductance but is impractical for hand assembly. Never use 0603 or larger in the RF path.
- **Dielectric:** Use **C0G (NP0)** for all RF-path capacitors. C0G has near-zero temperature coefficient and no piezoelectric effect. Avoid X7R in the RF path.
- **AC coupling caps:** 50 Ω impedance matching is critical. Calculate the correct value; do not copy values from low-frequency schematics.
- **Bypass/shunt caps:** Impedance matching is less critical; prioritize low ESL.

---

### PMA3-14LN+ Application Circuit Modifications

The Mini-Circuits evaluation board reference design (TB-PMA3-14LN+) is optimized for general use, not 10 GHz. The following changes are required:

| Reference | Original | Problem | Replacement |
|---|---|---|---|
| C1, C2 (DC blocks) | 10 nF / 0402 X7R | SRF ~20–30 MHz → acts as inductor at 10 GHz, adds ~40 Ω reactance | **22 pF / 0201 C0G** |
| L1, L2 (bias chokes) | 900 nH / 0402 | SRF ~760 MHz → acts as capacitor or lossy resistor at 10 GHz | **Option A:** 10–15 nH / 0402–0201 (true inductor at 10 GHz) **Option B (preferred):** quarter-wave stub on PCB (zero-component, lowest loss) |
| C5, C6 (choke bypass) | (standard value) | First line of defence against 10 GHz energy leaking through L1/L2 | **2.2–4.7 pF / 0201 C0G**, placed as close as physically possible to the cold end of L1/L2 |
| C3, C4 (input match) | 0.2 pF / 0.1 pF | These tiny values confirm the match is substrate-dependent | Recalculate based on actual PCB trace geometry and substrate |
| C7, C8 (bulk bypass) | 100 nF | Targets low-frequency supply noise; SRF is irrelevant here | No change |

---

### LTC5548 Mixer Application Circuit Modifications

The LTC5548 evaluation schematic uses standard values that must be updated for 10 GHz:

| Reference | Original | Change | Reason |
|---|---|---|---|
| C1, C3 (RF/LO match) | 0.15 pF / 0402 AVX ACCU-P | **No change** | ACCU-P series has ±0.05 pF tolerance and ultra-high Q — correct choice |
| C2, C4 (RF/LO bypass) | 22 pF / 0402 | Drop C2 to **2–4.7 pF** | Reduces parasitic inductance; 22 pF resonates below 10 GHz on 0402 |
| C4 (LO match) | 22 pF | **No change** | Impedance matching is already calculated for this position |
| C5 (supply bypass) | 1 µF / 0603 | **No change** | Low-frequency bulk bypass; SRF not critical here |
| T1 (IF balun) | TC1-1-13M+ | Select based on IF bandwidth: **TC1-1-13M+** for IF 4.5 MHz–3 GHz, **TCM1-83X+** for IF 10 MHz–6 GHz | Match IF bandwidth to your Doppler frequency range |

---

### HMC1163 VCO Bypass & Decoupling

| Pin / Net | Recommendation | Reason |
|---|---|---|
| V_TUNE | 1–10 pF shunt cap to GND, placed at pin | Shunts any residual 10 GHz leakage; this pin carries only the low-frequency tuning voltage — if 10 GHz is present here the chip is defective |
| Vcc | 100 pF (0402) in parallel with 2.2 µF Tantalum | 100 pF targets 10–500 MHz supply noise; Tantalum provides silent bulk energy without piezoelectric effects |
| X7R bypass caps (vibration environments) | Replace with C0G or Tantalum | X7R is piezoelectric — mechanical vibration (drone/vehicle) modulates the VCO, creating ghost targets |

**ADF4159 PCB noise control:**

- Route SPI lines (CLK, DATA, LE) on a different PCB layer or physically far from the CP (charge pump output) and V_TUNE pins.
- Place **33–47 Ω series resistors** on CLK, DATA, and LE close to the STM32 to damp digital edge rates and reduce RF radiation from the traces.
- Design the loop filter for a charge pump current of **2.5 mA or 2.81 mA**, then use the programmable charge pump current register to fine-tune the PLL bandwidth in firmware.

---

## PCB Design

### 1. Substrate Selection

| Frequency | Substrate | Notes |
|---|---|---|
| ≤ 6 GHz | FR-4 | Acceptable for short traces. Cheap, universally available. |
| 10 GHz | **Rogers 4350B** (or Isola i-Speed) | FR-4 is too lossy and has unpredictable Er. Rogers 4350B processes like FR-4 but has controlled Er and low loss tangent. |
| 24 GHz | Rogers RO3003 / RO4835 (PTFE-based) | FR-4 signal will vanish before reaching the connector. PTFE substrates are soft, hard to manufacture, expensive. |

Specifications for PTFE Teflon PCB on JLCPCB
PCB Thickness：0.76/1.52
PCB Laminates: ZYF255DA(Dk=2.55, Df=0.0018)/ ZYF265D(Dk=2.65, Df=0.0019)/ ZYF300CA-C（Dk=2.94, Df=0.0016)/ ZYF300CA-P(Dk=3.00, Df=0.0018）

---

### 2. Connector Launch

Getting the signal from a flat PCB trace into a coaxial connector without reflection is the single hardest part of the design.

| Frequency | Connector | Risk if misdesigned |
|---|---|---|
| 6 GHz | Standard edge-launch SMA | ~0.5 dB loss — acceptable |
| 10 GHz | High-quality SMA + **GCPW layout with ground via stitching** | –10 dB reflection (VSWR 2:1) — bad but functional |
| 24 GHz | **2.92 mm K-Connectors** (~$50 each) + 3D EM simulation | Even 0.1 mm gap error turns the connector into a capacitor |

---

### 3. Trace Dimensions & Etching Tolerances

At 10 GHz, trace impedance is highly sensitive to width. A 1 mil etch error at 24 GHz represents a 10–20% width deviation and a massive impedance mismatch. At 10 GHz the tolerance is more relaxed but still requires careful design.

At 10 GHz, the **skin effect depth is approximately 0.66 µm**. At 24 GHz this shrinks to ~0.4 µm, making surface copper roughness a significant loss mechanism. Specify low-roughness copper (HVLP or RTF) for high-frequency boards.

---

### 4. Vias

| Frequency | Signal Vias |
|---|---|
| 6 GHz | Acceptable |
| 10 GHz | Avoid if possible; model carefully if unavoidable |
| 24 GHz | **Do not use** — route everything on one layer |

Ground-stitching vias are still required and beneficial at all frequencies — these are short and their parasitic inductance is well-controlled.

---

### Grounded Coplanar Waveguide (GCPW)

At 10 GHz, **GCPW is preferred over standard microstrip** because it:
- Provides better shielding (ground planes on the same layer flank the signal trace)
- Suppresses substrate modes
- Has lower radiation loss

**Tapering:** Where the GCPW trace width differs from the connector pin diameter, use a **linear taper** to smoothly transition between the two widths and minimise impedance discontinuities.

**Via stitching:** Place ground vias along both sides of the signal path at intervals **< λ/10** (< 3 mm at 10 GHz) to prevent the coplanar ground from resonating.

---
