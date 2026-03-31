
---

## Electrical Components

### Core Board — Rapidfire Artix-7 100T

The only modification to the stock core board is:

- **R17**: changed from 750 kΩ → **550 kΩ** on the switching voltage regulator to produce a **2.5 V** rail.
- Artix-7 banks 15 & 16 must be supplied by 2.5 V for MIPI I/O.
  - HS lines → `LVDS25`
  - LP lines → `LVCMOS25`

### Unused MGT Banks

MGT banks are reserved for GTP transceiver optical fibre connectors present on the Pro board. Since those connectors are unused in this design, all MGT supply pins must still be properly terminated per Xilinx (AMD) guidance:

| Pin | Recommended connection |
|---|---|
| `MGTAVCC` | Same supply as `VCCAUX` (typically 1.8 V or 2.5 V) |
| `MGTAVTT` | Same supply as `VCCAUX` |
| `VCCAUX` | Standard FPGA power requirement |

> **Note:** Floating or unconnected MGT supply pins can cause latch-up and damage the device.

---

## Electrical Design

### 1. Impedance Matching (IBIS Model)

The slope of the V–I curve from the IBIS model gives the pin's intrinsic output impedance:

```
[Model]  LVCMOS25_S_12_HR
|Voltage   I(typ)     I(min)     I(max)
 0.06      5.8710 mA  3.9941 mA  7.2020 mA
 0.30     25.1900 mA 18.2700 mA 31.8100 mA
```

```
ΔV / ΔI = (0.30 − 0.06) V / (25.19 − 5.87) mA ≈ 13 Ω  (internal pin impedance)
```

Source termination rule:

```
Internal Pin Impedance + Series Resistor = PCB Trace Impedance
       13 Ω             +     37 Ω        =       50 Ω
```

Add a **0402, 37 Ω** series resistor on each FPGA output to match the 50 Ω trace and eliminate reflection ringing.

**Placement note:** Because the core board connects via a board-to-board (BTB) connector, the 37 Ω resistor cannot be placed immediately at the FPGA pin. Placing it next to the BTB connector (vs. no resistor) must be evaluated. However, the IBIS model's slow rise time (~1–2 ns) means any trace shorter than ~60 mm (~2.5 inches) is below the transmission-line threshold — mid-line reflections are absorbed by the slow edge, so the signal still arrives cleanly at the FX3.

---

### 2. USB Routing Considerations

#### GPIF II Parallel Bus (FPGA ↔ FX3) — 100 MHz, 32-bit

| Requirement | Value |
|---|---|
| Impedance | 100 Ω differential pair |
| Length matching | Required (all 32 data lines + clock) |
| Spacing | 3W rule |

#### USB 3.0 Interface (FX3 ↔ Connector) — 5 Gbps

| Requirement | Value |
|---|---|
| Impedance | 90 Ω differential |
| AC coupling caps (SSTX) | 100 nF (0402 or 0201) |
| Cap routing | Keep differential pair **symmetrical** through cap pads |

**FX3 1.2 V Core Power:**

The FX3 core is PLL-sensitive. A voltage droop during a burst transfer will cause the PLL to unlock and reset the connection.

- Use a **solid 1.2 V power plane**.
- Place **bulk + ceramic decoupling capacitors** as close to the FX3 power pins as possible.

**PCB stackup (JLC04161H-7628):**

| Layer | Material | Thickness |
|---|---|---|
| L1 (outer) | 1 oz copper | 0.035 mm |
| Prepreg | 7628, RC 49%, 8.6 mil | 0.2104 mm |
| L2 (inner) | 0.5 oz copper | 0.0152 mm |
| Core | 1.3 mm H/HOZ | 1.0650 mm |
| L3 (inner) | 0.5 oz copper | 0.0152 mm |
| Prepreg | 7628, RC 49%, 8.6 mil | 0.2104 mm |
| L4 (outer) | 1 oz copper | 0.035 mm |

