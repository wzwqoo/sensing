
## Signal Integrity Simulation

### 1. FX3 → USB Host (5 Gbps)

Simulation type: **Bit-by-Bit (Time Domain)** using standard IBIS models (no AMI model required).

**Workflow:**

| Step | Action |
|---|---|
| Stimulus | PRBS7 pattern (captures ISI; short simulation time) |
| Transmitter model | `CYUSB3014` IBIS model |
| Channel | PCB traces + vias + USB connector as S-parameter files (`.s4p`) |
| Receiver equaliser | Apply USB 3.0 Reference CTLE — without it the eye will appear closed at 5 Gbps |
| Pass criteria | Eye Height > 100 mV and Eye Width > 0.4 UI (after CTLE) |

---

### 2. Camera → FPGA (LVDS / MIPI)

Simulation type: **Bit-by-Bit**, source-synchronous.

- Use the camera's IBIS model (TX) and the FPGA's IBIS model (RX).
- Set the MIPI D-PHY transmitter data rate to **904.5 Mbps** per lane.
- The clock lane and at least one data lane must be simulated **simultaneously** to measure data-vs-clock skew.
- Primary failure modes:
  - **Crosstalk / skew** between the `+` and `−` traces of a differential pair (intra-pair skew).
  - **Setup/Hold timing** violation at the FPGA CSI-2 Rx inputs.

---

### 3. GPIF II Parallel Bus

Simulation type: **Crosstalk** (power-sum).

Although the bus is "only" 100 MHz, 32 lines switching simultaneously generates significant noise.

- Simulate one "victim" trace with all 31 other "aggressor" traces switching.
- Check that the victim remains below the `Vil` threshold.
- Use the **3W spacing rule** to mitigate near-end and far-end crosstalk.

---
## Electrical Simulation

### 1. Power Integrity (PI)

| Analysis type | What to check |
|---|---|
| DC IR Drop | Power plane copper sufficient to deliver current to FPGA pins without dropping below operating threshold |
| AC PDN Impedance (sweep) | PDN impedance < target impedance up to Nyquist frequency of data rate |
| Transient PI | Voltage rail behaviour during bursty sensor start/stop events |

> **Thermal coupling:** Processing at 200 fps causes the FPGA to run hot. Copper resistivity increases with temperature, worsening IR drop — include a thermal-aware PI simulation.

---

### 2. Signal Integrity (SI) — Detailed

#### Propagation Delay & Skew

For a synchronous bus (32 data + 1 clock) all bits must arrive within the clock's setup/hold window:

- **Goal:** skew (fastest bit vs. slowest bit arrival) ≪ clock period.
- At 100 MHz (10 ns period), a 1 ns skew already consumes 10 % of the timing budget.
- The clock trace is typically slightly *longer* than the data traces so data is stable before the clock edge arrives.

#### Overshoot & Ringing — Transient IBIS Simulation

FPGA output pins have fast edge rates. On a 32-bit bus, reflected edges cause ringing that can double-clock the FX3.

- **Fix:** series termination resistors (22 Ω or 33 Ω) near the FPGA output pins.
- If simulation shows overshoot exceeding the FX3 absolute maximum input voltage, resistors are mandatory.

#### Crosstalk — Power-Sum Analysis

In a 32-bit bus, all 31 neighbouring traces act as aggressors on any one victim trace.

- Use **Power-Sum Crosstalk** in SIPro/ADS to calculate cumulative interference.
- Check: does the victim stay below `Vil` (≈ 0.4–0.8 V) when all 31 aggressors switch simultaneously?

#### SSN / Ground Bounce

32 pins switching at once demand a burst of current from the PDN, causing the ground plane to "bounce".

- Include **power and ground planes** in the SIPro simulation setup.
- If the eye closes more with 32 bits switching vs. 1 bit, you have an SSN problem.
- **Fix:** additional decoupling capacitors and/or reduced driver strength.

#### Eye Diagram — Full Channel

End-to-end channel: `[IMX477 TX] → [Extension Board] → [BTB Connector] → [Main Board] → [FPGA RX]`

- Model each segment as S-parameter blocks and connect in series.
- Place an `Eye_Probe` at the FPGA pins.
- Pass/fail based on eye height and width meeting the receiver's CDR lock requirements.

#### USB 3.0 Specific Checks

| Item | Requirement |
|---|---|
| AC coupling caps (SSTX) | 100 nF; use 0201/0402; cut ground plane under cap pads to maintain 90 Ω |
| Connector | Always use manufacturer S-parameter model (Molex, Amphenol, etc.) |
| Via stubs | At 5 Gbps, unused via stubs act as antennas; back-drill or keep USB traces on a single layer |

#### Additional Analyses

| Analysis | Purpose |
|---|---|
| Thermal Simulation | Copper resistivity ↑ with temperature → worsens IR drop |
| EMI/EMC Simulation | Required for FCC/CE certification; model E-fields around high-speed traces |
| TIE Jitter Analysis | Ensure Time Interval Error does not exceed the SERDES UI budget |

---