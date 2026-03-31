
---
## 3-Way Power Splitter

A 3-way **Wilkinson splitter** is implemented on the PCB to distribute TX power to the antenna and two mixer LO ports.


| Parameter | Value |
|---|---|
| Z0 (system) | 50 Ω |
| Z1 (quarter-wave arms) | 89 Ω |
| Z2 (output sections) | 52 Ω |
| Isolation resistors | 100 Ω |
| Board size | 30 mm × 35 mm |
| Substrate | FR-4 (within tolerance at 10 GHz for this passive structure) |

**EM simulation results (ADS / CST):**

After optimisation, the phase imbalance across output ports is < 0.3°:

| Port | Phase (optimised) |
|---|---|
| Port 2 | 63.954° |
| Port 3 | 63.679° |
| Port 4 | 63.814° |

**Phase trim:** If one output path is electrically ahead (physically shorter), lengthen that trace or clip the other traces. A 5.75° phase error at 10 GHz corresponds to:

$$\Delta L = \frac{5.75°}{360°} \times \lambda_g$$

Calculate $\lambda_g$ from the actual substrate Er and add/remove trace length accordingly.

---

## Parallel Coupled Microstrip Coupler (16 dB)

Used to sample a small portion of the TX signal for phase reference or for feedback.

**Substrate:** PTFE (20 mil / 0.508 mm standard), or Isola i-Speed (0.1 mm core, Er = 3.63 for consistency with main board).

**Design parameters for 16 dB coupling in a 50 Ω system:**

$$k = 10^{-16/20} \approx 0.158$$

$$Z_{0e} = 50 \times \sqrt{\frac{1+k}{1-k}} \approx 58.6\ \Omega$$

$$Z_{0o} = 50 \times \sqrt{\frac{1-k}{1+k}} \approx 42.7\ \Omega$$

Enter $Z_{0e}$ and $Z_{0o}$ into a microstrip calculator (TXLine, ADS LineCalc, or online) with your substrate parameters to obtain physical **Width (W)** and **Gap (S)**.

**LineCalc result (Isola i-Speed, Er = 3.63, H = 0.1 mm):**

| Parameter | Value |
|---|---|
| W | 6.978 mil |
| S (gap) | 4.566 mil |
| L (length) | 182.5 mil (~4.5 mm) |
| Calculated C_DB | –16.08 dB ✓ |

**Length:** Quarter-wavelength at 10 GHz. In free space $\lambda = 30\ \text{mm}$; on this substrate the effective wavelength is shorter:

$$L = \frac{\lambda_0}{4\sqrt{\varepsilon_{eff}}} \approx 4\ \text{to}\ 5\ \text{mm}$$

**Isolated port:** Terminate with a precision 0402 50 Ω resistor (e.g., Vishay FC0402 series) placed immediately at the port pad.

---
