# 10 GHz 1TX/3RX MIMO CW Radar

A continuous-wave (CW) radar system operating at 10 GHz designed to measure **ball speed**, **spin rate**, and **spin axis** orientation. Range, elevation, and azimuth are imported from an external stereo camera or a 24 GHz FMCW radar.

## Lessons Learnt: 10 GHz FMCW Radar PCB Design
## 1TX 3RX 2-Layer PTFE Board

---

## JLCPCB PTFE Laminate Specifications

| Laminate | Dk | Df | Thickness Options |
|---|---|---|---|
| ZYF255DA | 2.55 | 0.0018 | 0.76 mm / 1.52 mm |
| ZYF265D | 2.65 | 0.0019 | 0.76 mm / 1.52 mm |
| ZYF300CA-C | 2.94 | 0.0016 | 0.76 mm / 1.52 mm |
| ZYF300CA-P | 3.00 | 0.0018 | 0.76 mm / 1.52 mm |

All laminates are PTFE (Teflon) based. Low Df values make them
suitable for 10 GHz but the limited thickness options create
significant microstrip design constraints as described below.

---

## Lesson 1: PCB Thickness Causes Trace Width Problems

### What happened

JLCPCB only offers PTFE in **0.76 mm and 1.52 mm** thickness.
Microstrip characteristic impedance for 50Ω is governed by:

```
Z0 ≈ (87 / sqrt(Dk + 1.41)) × ln(5.98h / (0.8w + t))

Where:
  h = substrate height (PCB thickness)
  w = trace width
  t = trace thickness (copper weight)
  Dk = dielectric constant of laminate
```

For **ZYF265D (Dk = 2.65)** at **1.52 mm thickness**:

```
Solving for 50Ω:
  w ≈ 4.2 mm

For 0.76 mm thickness:
  w ≈ 2.1 mm
```

### Why this is a problem at 10 GHz

A **4.2 mm wide trace** at 10 GHz is not electrically narrow.
The free-space wavelength at 10 GHz is 30 mm. On ZYF265D the
guided wavelength is:

```
λg = λ0 / sqrt(Dk_eff)
   ≈ 30 / sqrt(2.1)    (effective Dk for microstrip)
   ≈ 20.7 mm
```

A 4.2 mm trace width is **λg/5** — approaching the regime where
the trace starts to behave as a waveguide rather than a
transmission line. Higher-order modes can propagate, causing:

- Radiation loss at bends and discontinuities
- Impedance mismatch at connector launches
- Pattern distortion on antenna feed lines

### What to do instead

**Option A:** Use **0.76 mm thickness** to bring trace width to
~2.1 mm. Still wide but more manageable. Tradeoff is thinner
board is harder to handle and solder.

**Option B:** Use **ZYF300CA-P (Dk = 3.00)** at 0.76 mm. Higher
Dk gives narrower trace for the same impedance:

```
ZYF300CA-P, 0.76mm, 50Ω:
  w ≈ 1.8 mm    ← significantly narrower
```

**Option C:** Switch to a lower Dk laminate available in thinner
profiles from other manufacturers (Rogers RO4003C 0.508mm gives
~0.9mm trace width at 50Ω). Accept higher cost and longer lead
time outside JLCPCB.

**Option D:** Use **grounded coplanar waveguide (GCPW)** instead
of microstrip. GCPW adds ground planes on both sides of the
trace on the top layer, which tightens the field confinement and
allows a narrower trace width for the same 50Ω impedance:

```
GCPW 50Ω on ZYF265D 1.52mm:
  trace width w ≈ 1.2 mm
  gap to ground g ≈ 0.3 mm
```

GCPW also reduces radiation loss and is the preferred topology
for 10 GHz PCB designs on thick substrates.

### Key takeaway

> Always calculate trace width **before** ordering the laminate.
> On JLCPCB PTFE, 1.52 mm forces impractically wide 50Ω traces
> at 10 GHz. Use 0.76 mm thickness and GCPW topology to keep
> RF trace widths below 2 mm.

---

## Lesson 2: 2 Layers with 3 RX Destroys Ground Plane Integrity

### What happened

A 2-layer board allocates:
- **Layer 1 (top):** RF traces, antenna elements, components
- **Layer 2 (bottom):** Ground plane

With **1 TX and 3 RX antennas** on the top layer, routing the
digital control signals (SPI to ADF4159, GPIO, power) requires
either running them on the top layer between RF structures or
passing them through vias to the bottom ground plane layer.

Neither option is acceptable at 10 GHz:

**Problem A: Vias interrupt ground plane continuity**

Every via drilled through the board removes copper from the
bottom ground plane. At 10 GHz the ground plane is not just a
DC reference — it is the return current path for every
microstrip trace above it. Return current flows in a narrow
band directly beneath each signal trace:

```
Top layer:    ─────── signal trace ───────
                           │
              ← return current width ≈ 3h →
Bottom layer: ════════ ground plane ════════
```

When vias punch through the ground plane, the return current
is forced to detour around the via hole. This detour creates
a local impedance discontinuity and radiates. With many vias
for 3 RX routing the ground plane becomes a patchwork of holes,
completely defeating its purpose.

**Problem B: No layer for digital routing isolation**

On a 2-layer board all digital signals (SPI clock, MOSI, GPIO
for chirp timing, power supply traces) must share the top layer
with the RF structures. Digital switching noise couples
directly into the sensitive RX signal paths. At 10 GHz even
a few millivolts of injected noise corrupts the mixer output.

**Problem C: Antenna isolation is compromised**

3 RX antennas need physical separation for adequate isolation
(typically >20 dB between adjacent elements). On a 2-layer
board the only isolation mechanism is physical distance on
the top layer. With routing traces and vias filling the space
between antennas the isolation degrades further.

### Measured consequence

In the actual build the bottom ground pour removal from via
routing reduced RX-to-RX isolation by an estimated 8–12 dB
compared to simulation. The noise floor rose correspondingly,
reducing effective SNR on all three RX channels.

### What to do instead

**Use 4 layers minimum for any 10 GHz MIMO radar:**

```
Layer 1 (top):    RF traces, antennas, RF components only
Layer 2:          Solid unbroken ground plane
Layer 3:          Power distribution
Layer 4 (bottom): Digital signals, low-frequency routing
```

This gives:
- Unbroken ground plane under all RF traces (Layer 2)
- Complete separation of RF and digital domains
- Power plane shielding between RF and digital layers
- Via transitions only where absolutely necessary
  with ground stitching vias around each signal via

**Via stitching rule for 10 GHz:**

Place ground stitching vias around every signal via at a
spacing of **λg/20 or less**:

```
λg at 10 GHz on ZYF265D ≈ 20.7 mm
Max stitching via spacing = 20.7 / 20 ≈ 1.0 mm
```

On JLCPCB PTFE the minimum via drill is 0.2 mm with 0.4 mm
pad — dense stitching is achievable but must be planned in
layout from the start.

### Key takeaway

> 2 layers is fundamentally incompatible with a 3 RX MIMO
> radar at 10 GHz. The ground plane is destroyed by routing
> vias. Use 4 layers with a dedicated unbroken ground plane
> on Layer 2 and all digital routing on Layer 4.

---

## Lesson 3: No I/Q Data — Why the Original Architecture Was Wrong

### The original assumption

The first design captured **real IF data only** — one ADC
channel per RX antenna, no quadrature. This seemed simpler:
fewer ADC channels, simpler hardware, lower data rate.

### Why real-only IF is insufficient for this radar

**Problem 1: Cannot distinguish positive from negative Doppler**

With real IF the spectrum is symmetric around DC. A target
moving toward the radar and a target moving away at the same
speed produce identical IF frequencies. You cannot tell
approach from recession.

For a golf ball in flight this is critical — the ball has
a large positive Doppler (approaching) but the spin sidebands
are symmetric and fold on top of each other. The micro-Doppler
signature is uninterpretable without I/Q.

For vital signs the heartbeat signal is a phase modulation
of approximately ±0.3 mm chest displacement. Without I/Q
phase recovery you cannot extract this signal from noise.

**Problem 2: DC offset from LO self-mixing**

In a homodyne (zero-IF) CW radar the LO leaks through the
mixer and mixes with itself, creating a large DC component
that saturates the ADC and buries the target signal. I/Q
demodulation followed by digital DC removal cleanly separates
the DC component from the signal. With real IF only the DC
leakage and the near-zero-Doppler target signals are
inseparable.

**Problem 3: SNR loss**

A real-only receiver throws away half the signal information
(the imaginary component). This is a fundamental 3 dB SNR
penalty compared to a coherent I/Q receiver. At 10 GHz
operating range margins are tight enough that 3 dB matters.

### Why ADAR7251 + OctoSPI was considered then rejected

The initial approach was:

```
4 RX antennas
  │
  ▼
I/Q split at RF (90° hybrid per channel)
  │
  ▼ 8 signals (4× I + 4× Q)
  │
ADAR7251 ×2 (8 channels total)
  │
  ▼
STM32H723
  SAI capture ──► OctoSPI ──► external buffer ──► USB ──► host
```

**Why OctoSPI is not practical for ADC data capture:**

OctoSPI on the STM32H723 is designed for one specific purpose:
communicating with NOR flash and HyperRAM using a
command/address/data transaction protocol. It is always the
**bus master** — it initiates every transaction.

The ADAR7251 in serial master mode **pushes data continuously**
driven by its own internal clock (SCLK_ADC). It does not
wait to be asked. It does not respond to commands. It is
also a master.

```
OctoSPI model:
  STM32 sends:  [command][address][data burst]
  Device responds passively

ADAR7251 model:
  Device drives: SCLK_ADC continuously at 38.4 MHz
  Device outputs: FS_ADC + DOUT0 + DOUT1 free-running
  STM32 must be: a passive slave receiver
```

These two models are incompatible. OctoSPI has no slave mode.
You cannot use it to receive a free-running serial stream.

OctoSPI is only useful **after** capture — as a fast path to
external HyperRAM for buffering, or as an output channel to
another device. It plays no role in ADC data capture.

**Why 4× SAI blocks on STM32H723 is painful:**

With 2× ADAR7251 outputting 4 serial data lines (DOUT0 and
DOUT1 from each chip) you need 4 simultaneous serial
receivers. The STM32H723 has exactly 4 SAI blocks
(SAI1_A, SAI1_B, SAI2_A, SAI2_B). Using all of them means:

- Every SAI peripheral on the chip is consumed
- Synchronisation between 4 blocks requires careful
  master/slave configuration and still has startup skew
- 4 independent DMA streams must be coordinated
- Any future use of SAI (audio codec, another sensor)
  is impossible
- The firmware complexity is high with many failure modes

This is the entire SAI peripheral budget of the chip spent
on one function, leaving no margin.

### Key takeaway

> Real IF data is insufficient for micro-Doppler spin
> detection and vital signs. I/Q is mandatory. OctoSPI
> cannot capture ADC data streams — it is a memory
> interface not a serial receiver. Using all 4 SAI blocks
> on STM32H723 for 8-channel capture is technically possible
> but consumes the entire peripheral budget and is fragile.

---

## Lesson 4: Why Move from STM32H723 to Artix-7 FPGA

### The STM32H723 peripheral bottleneck

Every additional RX channel or I/Q split costs one dedicated
serial capture peripheral. The mapping is rigid:

```
STM32H723 SAI blocks: 4 total
ADAR7251 data lines:  4 (for 2× chips, 2ch/line mode)
Remaining SAI:        0
```

Adding a fifth data line (e.g. a reference channel or a
third ADAR7251) is impossible without an external serialiser
or a complete architecture change.

The STM32 is fundamentally a microcontroller — its peripherals
are fixed at silicon design time. You cannot add a fifth SPI
slave port or a sixth SAI block in firmware.

### What the Artix-7 changes

An FPGA has no fixed peripherals. Every I/O pin can be a
clock input, a data input, or a control signal. The
capture logic is written in HDL and synthesised into the
fabric. Adding an eighth, ninth, or sixteenth channel
means editing the HDL and re-synthesising — no hardware
change required.

```
Artix-7 XC7A100T resources available:
  I/O pins:      300  (need ~30 for 2× ADAR7251)
  LUT:           101,440
  BRAM:          135× 36Kb = 4.86 Mb  (deep FIFO for free)
  DSP48 slices:  240  (range FFT onboard)
  GbE hard MAC:  yes
```

**Synchronisation is solved by construction:**

In an FPGA all flip-flops capturing ADAR7251 data share
the same clock domain — SCLK_ADC from the ADAR7251 master
routed through a single BUFG global clock buffer. All 4
data lines are captured on the same clock edge with zero
skew between channels. No software calibration needed.

```vhdl
-- All 4 data lines captured identically, same clock
process(sclk_adc)
begin
    if rising_edge(sclk_adc) then
        sr_ch12 <= sr_ch12(30 downto 0) & dout0_adc1;
        sr_ch34 <= sr_ch34(30 downto 0) & dout1_adc1;
        sr_ch56 <= sr_ch56(30 downto 0) & dout0_adc2;
        sr_ch78 <= sr_ch78(30 downto 0) & dout1_adc2;
    end if;
end process;
```

**BRAM provides deep elastic FIFO for free:**

The ADAR7251 writes at 38.4 MHz (SCLK_ADC domain). The
host interface reads at its own clock (USB or Ethernet).
On an FPGA an asynchronous FIFO between the two clock
domains is a standard primitive — Xilinx FIFO Generator
produces it in minutes. On the STM32 bridging two clock
domains requires external FIFO chips or complex DMA
double-buffering.

**Optional onboard range FFT:**

The Artix-7 DSP48E1 slices can implement a pipelined
radix-2 FFT. A 1024-point complex FFT uses approximately
55 DSP48 slices. With 240 available you can run range FFT
on all 8 channels simultaneously in real time, sending
only the magnitude spectrum (or CFAR detections) to the
host rather than raw samples. This reduces host interface
bandwidth by 4–10× depending on detection density.

**The recommended split:**

```
STM32H723                    Artix-7 FPGA
─────────────────            ──────────────────────────
ADF4159 SPI control  ──►     ADAR7251 serial capture
HMC1163 bias GPIO            4× shift registers
Chirp timing (TIM)           BRAM FIFO (elastic buffer)
System state machine         Optional range FFT (DSP48)
                             GbE UDP packetiser
                    ◄──SPI── Status / config from STM32
```

STM32 handles the slow control plane it is designed for.
Artix-7 handles the high-speed data plane it is designed for.
Clean separation. No peripheral budget conflicts.

### Key takeaway

> The STM32H723 runs out of SAI peripherals at 4 data lines
> and has no path to expansion. The Artix-7 captures any
> number of serial data lines in a single clock domain with
> perfect synchronisation, provides deep FIFO in BRAM, and
> can optionally process data onboard. Moving to FPGA for
> the data plane is not complexity for its own sake — it
> removes the peripheral bottleneck entirely.

---

## Lesson 5: Data Rate Analysis — Is USB 2.0 HS Enough?

### Setup: 4 RX antennas with full I/Q

Each RX antenna receives I and Q separately via a 90° hybrid
coupler at RF. Each I/Q pair feeds two ADAR7251 channels.

```
4 RX antennas × 2 (I and Q) = 8 ADC channels total
                             = 2× ADAR7251 fully utilised
```

### Raw data rate calculation

At maximum ADAR7251 sample rate with full 16-bit resolution:

```
Sample rate:     1.2 MSPS (DECIM_RATE = 011 in register 0x140)
Channels:        8
Word width:      16 bits

Raw data rate = 8 × 1,200,000 × 16 bits
              = 153,600,000 bits/second
              = 153.6 Mbps
              = 19.2 MB/s
```

At lower sample rates (adequate for vital signs / slow Doppler):

```
300 kSPS:  8 × 300,000 × 16 = 38.4 Mbps  =  4.8 MB/s
600 kSPS:  8 × 600,000 × 16 = 76.8 Mbps  =  9.6 MB/s
1.2 MSPS:  8 × 1,200,000 × 16 = 153.6 Mbps = 19.2 MB/s
1.8 MSPS:  8 × 1,800,000 × 16 = 230.4 Mbps = 28.8 MB/s
           (note: resolution degrades above 1.2 MSPS)
```

### Is USB 2.0 High Speed (480 Mbps) enough?

**Theoretical USB 2.0 HS bandwidth:** 480 Mbps = 60 MB/s

**Real-world USB 2.0 HS bulk transfer throughput:**
approximately 37–42 MB/s accounting for protocol overhead,
packet framing, and host latency.

```
Raw data rate at 1.2 MSPS:   19.2 MB/s
USB 2.0 HS real throughput:  ~40 MB/s
Headroom:                    2.1×  ← adequate
```

At 1.2 MSPS with 8 channels USB 2.0 HS is sufficient.
The 2× headroom handles burst overhead and leaves room
for metadata (timestamps, chirp counters, status bytes).

**However USB 2.0 HS becomes marginal if:**

```
1. Sample rate increases to 1.8 MSPS:
   28.8 MB/s raw + overhead → approaching 40 MB/s limit

2. Processing metadata is added per chirp:
   headers + CRC + timestamps add ~5% overhead

3. Host PC USB controller is busy:
   real throughput can drop to 25 MB/s on loaded systems

4. Future expansion to more channels:
   any additional ADAR7251 chips immediately exceed limit
```

### Verdict on USB 2.0 HS

```
Use case                          USB 2.0 HS      Recommended
──────────────────────────────────────────────────────────────
Vital signs, 300 kSPS, 8ch        Yes, 4.8 MB/s   USB 2.0 HS
Golf ball, 1.2 MSPS, 8ch          Yes, 19.2 MB/s  USB 2.0 HS
Max rate, 1.8 MSPS, 8ch           Marginal         USB 3.0
With onboard FFT, detections only Yes, <1 MB/s    USB 2.0 HS
Future 16ch expansion             No               USB 3.0 / GbE
```

**USB 2.0 HS is sufficient for the current 4 RX I/Q design
at up to 1.2 MSPS.** It becomes inadequate if you increase
sample rate beyond 1.2 MSPS or expand beyond 8 channels.

### Do you need USB 3.0 or Ethernet?

**USB 3.0 SuperSpeed (5 Gbps, ~300 MB/s real):**

Requires a USB 3.0 PHY chip (e.g. Cypress FX3 CYUSB3014)
connected to the FPGA via GPIF II. The FX3 handles the
USB 3.0 protocol stack. Overkill for 19.2 MB/s but gives
enormous headroom for future expansion. Adds ~$15 BOM cost
and non-trivial firmware on the FX3.

**Gigabit Ethernet (1 Gbps, ~94 MB/s real UDP):**

The Artix-7 has a hard Tri-Mode Ethernet MAC. Add an
88E1111 or KSZ9031 PHY ($3–5) and you have GbE. UDP
throughput of 94 MB/s is 4.9× the raw data rate. No
special driver needed on the host — standard socket.
Wireshark debugging for free. No USB cable length
limitations. Works over a network switch for remote
operation.

**Recommendation:**

```
For lab use and current 8-channel design:
  → USB 2.0 HS is sufficient at 1.2 MSPS
  → Implement on Artix-7 with Cypress FX2LP (USB 2.0)
     or direct ULPI PHY

For production or future expansion:
  → Gigabit Ethernet is the better long-term choice
  → Lower cost than USB 3.0 solution
  → No host driver complexity
  → Artix-7 hard GbE MAC makes this straightforward
  → 94 MB/s handles even 16-channel expansion at 1.2 MSPS
     (16 × 1.2M × 16bit = 38.4 MB/s, well within GbE)
```

### Key takeaway

> USB 2.0 HS at ~40 MB/s real throughput is adequate for
> 8 channels at 1.2 MSPS (19.2 MB/s) with comfortable
> headroom. It becomes marginal at 1.8 MSPS and fails
> beyond 8 channels. Gigabit Ethernet via the Artix-7
> hard MAC is the recommended long-term interface —
> lower cost than USB 3.0, simpler host integration,
> and sufficient headroom for future expansion to 16
> channels without hardware changes.

---

## Summary of All Lessons

| # | Problem | Root Cause | Fix |
|---|---|---|---|
| 1 | 50Ω traces too wide | 1.52mm substrate + low Dk | Use 0.76mm + GCPW topology |
| 2 | Ground plane destroyed | 2 layers insufficient for 3 RX routing | Minimum 4 layers, unbroken ground on L2 |
| 3a | No I/Q = poor Doppler | Real IF only captured | 90° hybrid at RF, 2 ADC channels per RX |
| 3b | OctoSPI cannot capture ADC | OctoSPI is master-only memory interface | Use SAI (STM32) or FPGA I/O (Artix-7) |
| 4 | STM32 peripheral exhaustion | 4 SAI blocks = entire budget for 8ch | Move data plane to Artix-7 FPGA |
| 5 | Interface bandwidth | 8ch I/Q at 1.2MSPS = 19.2 MB/s | USB 2.0 HS adequate now, GbE for future |

---

*Document generated from design review of 10 GHz FMCW radar prototype.*
*Laminates: JLCPCB PTFE series ZYF255DA / ZYF265D / ZYF300CA-C / ZYF300CA-P.*
*Revision 1.0*
