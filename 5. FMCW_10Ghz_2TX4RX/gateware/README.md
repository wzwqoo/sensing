# Golf Radar FPGA Pipeline — Artix-7 100T

End-to-end radar signal processing pipeline targeting the **Arty A7-100T** (`xc7a100tcsg324-1`).  
Detects golf ball (~-30 dBsm) and club head (~-20 dBsm) returns from a 4-antenna FMCW radar.

> **Platform constraint:** Artix-7 has no PS, no HP ports, no built-in DDR3 controller.  
> Everything is pure RTL. MIG 7 Series provides DDR3 access.

---

## Table of Contents

1. [Architecture overview](#1-architecture-overview)
2. [Clock domains and CDC](#2-clock-domains-and-cdc)
3. [Why TDM mux](#3-why-tdm-mux)
4. [Custom AXI4 masters](#4-custom-axi4-masters)
5. [DDR3 corner turn](#5-ddr3-corner-turn)
6. [CFAR HLS IP](#6-cfar-hls-ip)
7. [AXI interface reference](#7-axi-interface-reference)
8. [Vitis HLS directive guide](#8-vitis-hls-directive-guide)

---

## 1. Architecture Overview

### Custom RTL (packaged as IP in Vivado)

| Module | Resources | Role |
|---|---|---|
| `adar7251_ppi_rx` | Pure logic | Deserialises ADAR7251 SPI/PPI output → parallel samples |
| `tdm_mux_4to1` | Flip-flops | 4 RX channels → single TDM stream at 4×fs |
| `tdm_demux_1to4` | Flip-flops + SRL | Splits TDM stream back to 4 channels, tag-tracked |
| `iq_delay_align` | SRL shift regs | Aligns I path delay to match Hilbert (Q) pipeline depth |
| `scatter_write_master` | Logic + DSP (addr) | Range FFT → DDR3 transposed write + ping-pong control |
| `doppler_seq_ctrl` | Logic | DDR3 sequential burst reads → Doppler FFT input |
| `rd_map_collector` | Logic | Doppler FFT output → DDR3 RD map + kicks CFAR |
| `cfar_detector` | Logic + shift regs | 2D CA-CFAR from Vitis HLS export |

### Block Design IPs (Vivado IP Integrator)

| IP | Role |
|---|---|
| MIG 7 Series | DDR3 controller — complex PHY timing, AXI4 slave |
| AXI SmartConnect | Routes 4 AXI4 masters → single MIG slave |
| Xilinx xfft | Range FFT (1024-pt) and Doppler FFT (128-pt), AXI-Stream |
| axis_clock_converter | Single CDC crossing: 4.8 MHz → 100 MHz |
| Clock Wizard | Derives 4.8 MHz from board 100 MHz oscillator |
| Processor System Reset | Synchronised reset tree from MIG `mmcm_locked` |
| FIR Compiler | Hilbert FIR (26 DSPs) + BPF I and Q (52 DSPs each), TDM |

### Full pipeline data flow

```
ADAR7251 (1.2 MSPS × 4 ch)
  → PPI deserialiser
  → TDM MUX 4:1                  [4.8 MHz domain]
  → Hilbert FIR IP  (Q output)
  → iq_delay_align  (I = delayed input)
  → TDM DEMUX 1:4  (I[0-3], Q[0-3])
  → re-MUX 4:1     (I path, Q path)
  → BPF FIR IP — I
  → BPF FIR IP — Q
  → TDM DEMUX 1:4

  ── axis_clock_converter (4.8 → 100 MHz, single CDC) ──

  → Range FFT xfft (1024-pt)     [100 MHz domain — MIG ui_clk]
  → scatter_write_master          (transposed write, ping-pong A/B)
      ↕ DDR3
  → doppler_seq_ctrl              (128-beat sequential burst reads)
  → Doppler FFT xfft (128-pt)
  → rd_map_collector              (sequential write → RD map buffer)
      ↕ DDR3
  → cfar_detector HLS IP          (m_axi random reads, writes detections)
  → detections output
```

### DDR3 memory map

| Region | Base address | Size | Contents |
|---|---|---|---|
| Transposed buf A | `0x0010_0000` | 512 KB | Range FFT chirps, column-major |
| Transposed buf B | `0x0018_0000` | 512 KB | Ping-pong second buffer |
| RD Map | `0x0020_0000` | 512 KB | 1024×128 complex Doppler output |
| Detections | `0x0028_0000` | 128 KB | CFAR `det_map` output |

---

## 2. Clock Domains and CDC

### Why CDC is needed

The ADAR7251 outputs data clocked by its own sample-rate clock.  
The MIG DDR3 controller generates its own `ui_clk` (100 MHz for Arty A7).  
These two clocks are asynchronous — connecting them directly causes metastability.

### Clock plan

| Domain | Frequency | Source | What runs here |
|---|---|---|---|
| ADC / DSP | 4.8 MHz | Board 100 MHz → BUFR ÷ 10 via MMCM | Deserialiser, TDM MUX, Hilbert FIR, BPF FIR, Range FFT input |
| DDR3 / processing | 100 MHz | MIG `ui_clk` | scatter_write_master, doppler_seq_ctrl, rd_map_collector, CFAR, both xfft IPs |

### The single CDC crossing

One `axis_clock_converter` IP sits between the BPF output DEMUX and the Range FFT `s_axis_data` input.  
That IP contains the async FIFO internally — connect both clocks and it handles everything.

The data rate mismatch helps: BPF pushes at 4.8 MHz (one sample every ~208 ns).  
The Range FFT on the 100 MHz side consumes it 20× faster than it arrives, so the FIFO never fills and backpressure never reaches the FIR.

### `init_calib_complete` gate

MIG takes ~200 µs after reset to calibrate the DDR3 DRAM.  
`scatter_write_master` must **not** issue any AXI transactions until `mig_0/init_calib_complete` is high.  
Wire it as an enable into the write FSM's idle state.

---

## 3. Why TDM Mux

### The DSP budget problem

Artix-7 100T has **240 DSP48E1 slices**.

| Block | Instances | DSPs each | Total |
|---|---|---|---|
| Hilbert FIR (101-tap) | ×4 (naive) | 26 | 104 |
| BPF-I FIR (101-tap) | ×4 (naive) | 52 | 208 |
| BPF-Q FIR (101-tap) | ×4 (naive) | 52 | 208 |
| **Naive total** | | | **520 — 216% of budget** |

### The TDM fix

Run the DSP clock at **4×fs** (4.8 MHz). One FIR IP processes all 4 channels serially — one channel per clock cycle.

| Block | Instances | DSPs each | Total |
|---|---|---|---|
| Hilbert FIR | ×1 shared | 26 | 26 |
| BPF-I FIR | ×1 shared | 52 | 52 |
| BPF-Q FIR | ×1 shared | 52 | 52 |
| Range FFT | ×1 | ~24 | 24 |
| Doppler FFT | ×1 | ~10 | 10 |
| CFAR | ×1 | 4 | 4 |
| **TDM total** | | | **~168 — 70% of budget ✓** |

### Channel tag tracking

The TDM DEMUX uses a shift-register tag tracker of depth = FIR pipeline latency.  
The tag travels alongside the data through the FIR and tells the DEMUX which channel is at the output on any given cycle.  
If channels are routed to the wrong output, adjust `HILBERT_LATENCY` by ±1 — it is always an off-by-one from the MUX output register.

---

## 4. Custom AXI4 Masters

Artix-7 has no DMA engine built in. Three custom RTL modules implement AXI4 masters directly.  
Vivado IP Packager auto-detects the `m_axi_` port prefix and groups them into a bus interface when packaging.

### `scatter_write_master`

**Role:** Range FFT output → DDR3 transposed write. Includes integrated ping-pong controller.

**Why transposed?**  
Range FFT produces samples k=0..1023 for each chirp n.  
Doppler FFT needs all 128 chirp values for one range bin k.  
Storing transposed (column-major) makes Doppler reads 128-beat sequential bursts — optimal DDR3 bandwidth.

```
addr = buf_base + (k × N_CHIRPS + n) × SAMPLE_BYTES
```

Scattered writes are DDR3-unfriendly but only happen 131,072 times per frame and the MIG reorder buffer smooths it out. The payoff is near-peak DDR3 bandwidth on every Doppler read.

**AXI channels used:** AW + W + B (write-only master)  
**Burst type:** Single-beat (AWLEN=0) — transposed addresses are non-sequential, bursting is not possible  
**WLAST:** Tied `1'b1` — always last for single-beat writes

**Ping-pong FSM:**

| State | Write buffer | Read buffer | Exit condition |
|---|---|---|---|
| `PP_FILL_A` | A | none ready | 128 chirps complete → `PP_PROC_A_FILL_B` |
| `PP_PROC_A_FILL_B` | B | A (Doppler reading) | Frame complete + Doppler done → `PP_PROC_B_FILL_A`; frame complete + Doppler busy → `PP_STALL` |
| `PP_PROC_B_FILL_A` | A | B (Doppler reading) | Frame complete + Doppler done → `PP_PROC_A_FILL_B`; frame complete + Doppler busy → `PP_STALL` |
| `PP_STALL` | stalled | whichever Doppler is on | `doppler_frame_done` pulse → resume correct PROC state |

**Write FSM:**

| State | What happens | Exit condition |
|---|---|---|
| `W_IDLE` | Wait for Range FFT data. If ping-pong stalled, hold `tready` low to back-pressure FFT. | `s_axis_tvalid` high and not stalled → latch first sample, go `W_WRITE` |
| `W_WRITE` | Latch one sample from stream, issue AW+W. Use sticky flags (`aw_accepted_r`, `w_accepted_r`) to advance `k` only after **both** AW and W are accepted. | All 1024 bins issued → `W_DRAIN` |
| `W_DRAIN` | All writes issued. Wait for `outstanding == 0` — all B responses received, DDR3 has committed everything. | `outstanding == 0` → pulse `chirp_done`, go `W_DONE` |
| `W_DONE` | One-cycle gap for `chirp_done` to propagate into ping-pong FSM and chirp counter. | Always → `W_IDLE` |

> **Bug previously fixed:** Original code advanced `k` on AW acceptance alone. If W lagged, the next sample could overwrite `data_latch` before the old write completed. Now waits for both channels via sticky flags.

---

### `doppler_seq_ctrl`

**Role:** DDR3 sequential burst reads → Doppler FFT input stream.

**AXI channels used:** AR + R (read-only master)  
**Burst type:** 128-beat INCR (ARLEN=127, ARSIZE=2) — one burst per range bin, sequential addresses

| State | What happens | Exit condition |
|---|---|---|
| `S_IDLE` | Wait for `buf_valid` from scatter_write_master. | `buf_valid` high → `S_ISSUE_AR` |
| `S_ISSUE_AR` | Load registered burst address (`rd_addr_r`), set `arlen=127`, assert `arvalid`. Address is pre-registered one cycle to break multiplier timing path. | Always → `S_WAIT_AR` |
| `S_WAIT_AR` | Hold `arvalid` until MIG accepts. | `arvalid && arready` → `S_STREAM` |
| `S_STREAM` | Receive R-channel beats via skid buffer, forward to Doppler FFT. | `rlast` forwarded from skid → `S_WAIT_FFT` |
| `S_WAIT_FFT` | All 128 samples fed to FFT input. Wait for FFT to finish and assert `tlast` on its output. `rd_map_collector` consumes FFT output in parallel. | `fft_out_valid && fft_out_last` → `S_NEXT_BIN` |
| `S_NEXT_BIN` | Increment range bin `k`. | `k < 1023` → `S_ISSUE_AR`; `k == 1023` → `S_DONE` |
| `S_DONE` | Pulse `doppler_frame_done` — tells scatter_write_master the read buffer is free. | Always → `S_IDLE` |

> **Bug previously fixed:** Original S_STREAM asserted `rready`, accepted the AXI handshake, then dropped the data if the FFT was not ready. AXI4 R-channel handshake is irrevocable — once `rvalid && rready` fires, MIG advances its pointer. Fix: skid buffer captures `rdata` unconditionally on every accepted beat; `rready` only re-enables after the skid is drained to the FFT.

---

### `rd_map_collector`

**Role:** Doppler FFT output → DDR3 RD map (128-beat bursts), then kicks CFAR.

**AXI channels used:** AW + W + B (write-only master)  
**Burst type:** 128-beat INCR (AWLEN=127) — one burst per range bin, sequential addresses  
**WLAST:** Combinatorial wire `assign m_axi_wlast = m_axi_wvalid && (beat_cnt == BEAT_LAST)`

| State | What happens | Exit condition |
|---|---|---|
| `S_IDLE` | Auto-start on `s_axis_tvalid`. | `s_axis_tvalid` high → `S_ISSUE_AW` |
| `S_ISSUE_AW` | Load registered burst address, set `awlen=127`, assert `awvalid`. | Always → `S_WAIT_AW` |
| `S_WAIT_AW` | Hold `awvalid` until MIG accepts. Then open `s_axis_tready`. | `awvalid && awready` → `S_WRITE` |
| `S_WRITE` | Transfer one FFT sample per beat. `wlast` is a combinatorial wire — asserts automatically on beat 127. | 128 beats transferred → `S_WAIT_B` |
| `S_WAIT_B` | Wait for MIG write response. Without this, CFAR could read stale data if kicked too early. | `m_axi_bvalid` → `S_NEXT_BIN` |
| `S_NEXT_BIN` | Increment `k`. | `k < 1023` → `S_ISSUE_AW`; `k == 1023` → `S_KICK_CFAR` |
| `S_KICK_CFAR` | Pulse `cfar_start` for one cycle → CFAR `ap_start`. | Always → `S_WAIT_CFAR` |
| `S_WAIT_CFAR` | Wait for CFAR `ap_done`. | `cfar_done` high → pulse `frame_complete`, go `S_IDLE` |

> **Bug previously fixed:** `m_axi_wlast` was registered one beat early, producing a phantom 129th beat presented to MIG with `wlast=1`. Fixed by making it a combinatorial `output wire`.  
> **Bug previously fixed:** `wr_addr_r` had no reset clause — first burst went to address X. Fixed by adding `wr_addr_r <= RD_MAP_BASE` in reset.

---

## 5. DDR3 Corner Turn

### The problem

Range FFT writes data **row by row** (chirp 0, then chirp 1…).  
Doppler FFT needs data **column by column** (sample 0 of every chirp, then sample 1…).

A naive read would require strided accesses with stride = 4096 bytes — extremely DDR3-unfriendly (~5% bandwidth).

### The solution: write transposed

`scatter_write_master` writes each sample to its transposed address:

```
addr = buf_base + (k × 128 + n) × 4
```

where k = range bin index, n = chirp number.

After 128 chirps, all values for range bin k are physically contiguous in DDR3 at `buf_base + k × 512`. `doppler_seq_ctrl` then reads each bin with a single 128-beat sequential burst, hitting near-peak DDR3 bandwidth.

The scattered writes are accepted because MIG's internal reorder buffer handles non-sequential write traffic efficiently. The 131,072 scattered writes per frame complete in ~1.5 ms at 100 MHz.

### Ping-pong eliminates stalls

Two transposed buffers (A and B) alternate. While `doppler_seq_ctrl` reads from A, `scatter_write_master` fills B — true double-buffering with no pipeline stall between frames except in the edge case where the writer finishes B before the reader finishes A, in which case the writer stalls (back-pressures the Range FFT) until the reader releases.

---

## 6. CFAR HLS IP

### Algorithm: 2-pass separable CA-CFAR

A naive 2D sliding window over 1024×128 with 688 training cells would require all cells visible simultaneously. Instead the implementation decomposes into two separable 1D passes, each achieving II=1.

**Pass 1 — range axis:**  
For each of the 128 Doppler rows, a shift-register sliding window accumulates left and right training sums, skipping 2 guard cells each side. Result: `noise_map[128][1024]`.

**Pass 2 — Doppler axis:**  
A circular line buffer of 19 rows reads `noise_map` column by column, accumulating the Doppler training sum per range column. Detection avoids integer division:

```
CUT × N_TRAIN_TOTAL  >  α × col_sum   →  detection
```

`col_sum` already contains the range training sum from Pass 1, so it equals the full 2D noise estimate across all 688 training cells.

### Window geometry

```
Range window width  : 2×(N_GUARD_R + N_TRAIN_R) + 1 = 2×(2+16)+1 = 37
Doppler window height: 2×(N_GUARD_D + N_TRAIN_D) + 1 = 2×(1+8)+1  = 19
Guard box           : (2×N_GUARD_R + 1) × (2×N_GUARD_D + 1)       = 5×3 = 15
N_TRAIN_TOTAL       : 37 × 19 − 15 = 703 − 15 = 688
```

> **Correction from original notes:** The earlier version stated 614 training cells and showed `(37)(17) - (5)(3) = 629 - 15 = 614`. This was wrong. The Doppler window height is **19** (not 17), giving **688** training cells. The formula in `cfar_detector.h` was always correct; only the comment was wrong.

### Threshold factor α

```
α = N_TRAIN_TOTAL × (P_fa^(−1/N_TRAIN_TOTAL) − 1)

P_fa = 1e-4  →  α ≈ 12.3  →  alpha_q8 ≈ 3149   (golf ball, sensitive)
P_fa = 1e-5  →  α ≈ 19.7  →  alpha_q8 ≈ 5043   (design default)
P_fa = 1e-6  →  α ≈ 27.2  →  alpha_q8 ≈ 6963   (conservative)
```

`alpha_q8` is an `s_axilite` register — adjustable at runtime without reprogramming.

### HLS interface

| Port | Interface | Role |
|---|---|---|
| `rd_map` | `m_axi` master | Reads full 1024×128 RD map from DDR3 |
| `det_map` | `m_axi` master (same bundle) | Writes detection map to DDR3 |
| `alpha_q8` | `s_axilite CTRL` | Runtime threshold register |
| `return` | `s_axilite CTRL` | `ap_start` / `ap_done` / `ap_idle` handshake |

The `ap_start` input is driven by `rd_map_collector`'s `cfar_start` output — no processor involvement.  
The `ap_done` output feeds back to `rd_map_collector`'s `cfar_done` input.

### CFAR bugs fixed in C++ source

| Bug | Description | Fix |
|---|---|---|
| `left_sum` wrong index | `enter_left` pointed to `shift_reg[N_TRAIN_R]` — a guard cell, not a training cell | Changed to `shift_reg[N_TRAIN_R - 1]` |
| `exit_right` wrong index | Off by one — pointed inside the right training window instead of at the exit boundary | Changed to `shift_reg[R_TRAIN_START - 1]` |
| Prefill guard cell leak | `d >= N_GUARD_D` added guard row d=1 to `col_sum` | Changed to `d >= N_GUARD_D + 1` |
| `col_sum` frame bleed | Not re-zeroed between calls — values from previous frame leaked into next | Added explicit re-zero at start of every `doppler_cfar` call |
| Pipeline structure | Two inner loops under a pipelined outer loop — HLS cannot achieve II=1 | Merged load and compare into a single inner `RANGE_LOOP` |
| `N_TRAIN_TOTAL` comment | Comment said 614, formula computed 688 | Corrected comment to show 37×19−15=688 |

---

## 7. AXI Interface Reference

### When to use each protocol

| Protocol | Use when | Vivado generates wrapper? |
|---|---|---|
| AXI4-Stream | Continuous data flow, no addressing | Yes — `_S_AXIS.v` and `_M_AXIS.v` via Create AXI4 Peripheral |
| AXI4-Lite slave | Control registers — setting parameters, reading status | Yes — register file via Create AXI4 Peripheral |
| AXI4 full master | Random-access memory read or write (DDR3) | No — you write the full AW/W/B/AR/R logic |

### AXI4 full port list by channel

```
AW: awaddr  awlen  awsize  awburst  awvalid  awready
W:  wdata   wstrb  wlast   wvalid   wready
B:  bresp   bvalid bready
AR: araddr  arlen  arsize  arburst  arvalid  arready
R:  rdata   rresp  rlast   rvalid   rready
```

`awsize`, `awburst`, `arsize`, `arburst` must be present even if tied constant — MIG checks them and will reject a connection if they are missing.

### Which channels each module uses

| Module | AW | W | B | AR | R |
|---|---|---|---|---|---|
| `scatter_write_master` | ✓ | ✓ | ✓ | — | — |
| `doppler_seq_ctrl` | — | — | — | ✓ | ✓ |
| `rd_map_collector` | ✓ | ✓ | ✓ | — | — |
| `cfar_detector` (HLS) | ✓ | ✓ | ✓ | ✓ | ✓ |

### Vivado IP Packager auto-detection

When packaging RTL as IP, Vivado auto-detects bus interfaces by port name prefix:

| Prefix | Detected as |
|---|---|
| `m_axi_*` | AXI4 master bus interface |
| `s_axi_*` | AXI4 slave bus interface |
| `m_axis_*` | AXI-Stream master |
| `s_axis_*` | AXI-Stream slave |

What the IP Packager GUI **can** do: rename interfaces, re-associate signals to bus groups, add parameters, edit port maps, add bus interface definitions for signals that already exist in the RTL.

What it **cannot** do: create a port that does not exist in the RTL. If you need a new signal, edit the Verilog and re-package.

---

## 8. Vitis HLS Directive Guide

### Directive Editor location

Right-click any function or loop label in the **Directive View** → **Insert Directive**.

Always choose **Directive File** as the destination — this writes to `directives.tcl` and keeps your C++ source clean.

### Label your loops

HLS can only target a loop in the GUI if it has a label:

```cpp
// Bad — GUI shows "Loop 1"
for (int i = 0; i < N; i++) { ... }

// Good — GUI shows RANGE_LOOP
RANGE_LOOP: for (int i = 0; i < N; i++) { ... }
```

### Common directives

**PIPELINE II=1**  
Overlaps loop iterations so the next starts before the previous finishes. Apply to the innermost loop (e.g., `RANGE_LOOP`). Do not apply PIPELINE to both an outer and inner loop simultaneously — HLS will override the outer and may produce unpredictable II.

**UNROLL**  
Converts loop iterations into parallel hardware. Apply to small loops like shift register updates. Leave Factor blank for full unroll; enter a number for partial unroll. Only unroll loops with a small, fixed trip count.

**ARRAY_PARTITION complete dim=1**  
Converts every element of an array into a separate register (flip-flop), allowing all elements to be read/written in a single clock cycle. Required for shift registers and line buffers that need parallel access. Without this, Vitis infers BRAM which allows only 1–2 reads per cycle.

**INTERFACE**  
Defines IP ports. Apply to top-level function arguments:
- `m_axi bundle=AXI_MEM offset=slave` for DDR3 pointer arguments
- `s_axilite bundle=CTRL` for scalar control arguments and `return`

**LOOP_TRIPCOUNT min=N max=N**  
Hint only — does not change hardware. Helps the synthesis report estimate latency when loop bounds are runtime variables.

### Key constraints for the CFAR IP

```cpp
// Inner range loop — must be pipelined, not the outer Doppler loop
RANGE_LOOP: for (int r = 0; r < RANGE_BINS; r++) {
#pragma HLS PIPELINE II=1

// Shift registers — must be fully partitioned for parallel access
cell_t shift_reg[2 * HALF + 1];
#pragma HLS ARRAY_PARTITION variable=shift_reg complete dim=1

// Line buffer — partition rows dimension for parallel row reads
acc_t line_buf[BUF_D][RANGE_BINS];
#pragma HLS ARRAY_PARTITION variable=line_buf dim=1 complete

// Column accumulators — cyclic partition for 8-way parallel access
acc_t col_sum[RANGE_BINS];
#pragma HLS ARRAY_PARTITION variable=col_sum cyclic factor=8 dim=1
```

---

## Build Steps

### 1. Generate MIG `.prj` file

Open Vivado → IP Catalog → MIG 7 Series → Customize → Run MIG wizard.  
Settings for Arty A7-100T:
- DDR3 SDRAM, 16-bit width, 256 MB
- Clock period: 6000 ps (167 MHz DDR3)
- PHY:controller ratio 4:1 → `ui_clk` = 100 MHz
- AXI data width: 128-bit, address width: 28-bit
- Input clock: 100 MHz (board oscillator)

### 2. Generate CFAR HLS IP

```tcl
vitis_hls -f scripts/run_hls.tcl
```

Check synthesis report for exact FIR Compiler latency — update `HILBERT_LATENCY` and `BPF_LATENCY` parameters in the Verilog accordingly.

### 3. Package custom RTL as IPs

For each RTL module: **Tools → Create and Package New IP → Package a specified directory**.  
Set vendor `radar_ip`, library `radar`. Vivado auto-detects `m_axi_`, `s_axis_`, `m_axis_` prefixes.

After packaging, verify in the **Bus Interfaces** tab that `m_axi_*` ports are classified as **AXI4** (not AXI4-Lite). Vivado occasionally mis-classifies when data width is narrow — set it explicitly if needed.

### 4. Build block design

```tcl
source scripts/radar_artix7_bd.tcl
```

Connect `axis_clock_converter` between BPF DEMUX output and Range FFT `s_axis_data` — this is the only CDC crossing.

### 5. Generate bitstream

```tcl
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
```

---

## Resource Summary (Artix-7 100T estimates)

| Resource | Used | Budget | % |
|---|---|---|---|
| DSP48E1 | ~168 | 240 | 70% |
| BRAM18 | ~40 | 270 | 15% |
| LUT | ~35,000 | 63,400 | 55% |
| FF | ~20,000 | 126,800 | 16% |

## Frame Timing (100 MHz, fs = 1.2 MSPS)

| Stage | Duration |
|---|---|
| 128 chirps × Range FFT (1024-pt) | ~109 ms (ADC limited) |
| scatter_write 128 chirps | ~1.5 ms (pipelined with FFT) |
| 1024 × Doppler FFT (128-pt) reads | ~0.2 ms (128-beat bursts) |
| 1024 × rd_map_collector writes | ~0.3 ms |
| CFAR (1024×128 cells) | ~1.3 ms @ 100 MHz |
| **Total processing after ADC** | **~3.3 ms** |
