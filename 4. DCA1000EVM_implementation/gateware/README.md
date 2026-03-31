# AWR1843BOOST → Artix-7 LVDS IQ Capture

## File Structure

```
awr1843_lvds_top.v      ← Top-level: wires all modules together
lvds_clk_rx.v           ← IBUFDS + BUFIO + BUFR + clock-active detector
lvds_lane_rx.v          ← IBUFDS + ISERDESE2 per data lane
bit_sync.v              ← Bitslip controller: finds 0xF800 training pattern
word_sync.v             ← Drops header, gates valid IQ words
frame_ctrl.v            ← SOF/EOF pulses from clock-active edges + sample counter
iq_deinterleave.v       ← Maps 4 lanes → RX0_I, RX0_Q, RX1_I, RX1_Q
async_iq_fifo.v         ← Gray-coded async FIFO, lvds_div_clk → user_clk
awr1843_lvds.xdc        ← Timing + pin constraints (edit pin locations!)
tb_awr1843_lvds_top.v   ← Testbench
```

## mmWave SDK Profile Settings to Match

Set these in your mmWave chirp profile and match the Verilog parameters:

| SDK Parameter         | Verilog Parameter                  | Example Value |
|-----------------------|------------------------------------|---------------|
| `numAdcSamples`       | `SAMPLES_PER_CHIRP`                | 256           |
| `adcSampleRate`       | (sets LVDS bit clock)              | 37.5 MSps     |
| `numRxAnt`            | `NUM_LANES`                        | 4             |
| LVDS data rate (DDR)  | `SERDES_RATIO = 8`                 | DDR           |
| Complex output        | lane assignment in iq_deinterleave | I+Q           |

## Artix-7 Bank Requirements

- **Bank voltage**: 1.8V (VCCO_xx = 1.8V) to match AWR1843 LVDS drive levels
- **IOSTANDARD**: `LVDS_25` for HR banks; `LVDS` for HP banks
- **DIFF_TERM**: `TRUE` (enables internal 100-ohm termination)
- **I/O column**: all LVDS data pins must be in the **same I/O column** as the
  LVDS clock so that BUFIO can reach all ISERDESE2 instances

## Calibration Sequence

1. Assert `rst_n = 0` → release → `rst_n = 1`
2. Send the "Start Frame" command from mmWave SDK (`rlSensorStart()`)
3. AWR1843 starts driving LVDS clock → `lvds_clk_active` goes high
4. `bit_sync` issues bitslips until 0xF800 pattern found → `bit_sync_locked` = 1
5. `word_sync` detects header → `word_sync_locked` = 1
6. IQ data flows: `iq_valid` strobes with each sample word in `user_clk` domain
7. `frame_start` pulses at each chirp start, `frame_end` at each chirp end

## Known Customization Points

- **SDR mode**: Change `ISERDESE2 DATA_RATE` to `"SDR"` and `SERDES_RATIO` to 4
- **1-RX mode**: Tie lane2/lane3 inputs to ground, reduce `NUM_LANES` to 2
- **IDELAY2**: Insert between IBUFDS and ISERDESE2 for fine per-lane alignment
- **8 lanes (4 RX)**: Duplicate lane_rx instances, extend iq_deinterleave
- **Replace async FIFO**: Drop in Xilinx `xpm_fifo_async` for better timing closure

## Simulation

```bash
# Icarus Verilog (functional, no Xilinx primitives)
# Replace ISERDESE2/BUFIO/BUFR/IBUFDS with behavioral models for iverilog.
# For full simulation with primitives, use Vivado xsim:

vivado -mode batch -source run_sim.tcl
```

Add to Vivado project with all .v files, set `awr1843_lvds_top` as top module,
add `awr1843_lvds.xdc` as constraints.
