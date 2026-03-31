
---
## Gateware Design (Vivado / Vitis)

### General Sequence

```
config
  → start write VDMA
  → enable CSI2-RX
  → trigger sensor I2C
  → wait frame (triple buffer)
  → start read VDMA
  → start VTC (XVtc_EnableGenerator)
```

---

### Bandwidth Calculation

| Format | Calculation | Bandwidth |
|---|---|---|
| Raw 1280×720 @ 200 fps, 16-bit | 1280×720×200×16 | **2.95 Gbps** |
| Y-only / Grayscale 640×480 @ 200 fps, 8-bit, ×2 cameras | 640×480×200×8×2 | **~105 MB/s** |
| USB 3.0 practical limit | — | ~400 MB/s |

Switching to Y-only (8-bit grayscale) halves the bandwidth and stays comfortably within USB 3.0 limits. ✓

---

### IP Block Pipeline

```
CSI-2 RX
   │ (10-bit RAW Bayer)
   ▼
SLICER  ──── strips LSBs, keeps 8 MSBs per pixel
   │ (8-bit RAW Bayer)
   ▼
Demosaic IP  ──── Zhang's Bayer → 24-bit RGB
   │ (must write: width, height, Bayer phase, Start bit to control registers)
   ▼
Gamma LUT  ──── luminance/colour adjustment via LUT
   │ (in Vitis: generate curve array → write XV_gamma_lut registers → set Start bit)
   ▼
Video Process Subsystem  ──── RGB → YUV4:2:2
   │
   ▼
AXI Subset Converter  ──── YUV4:2:2 → Mono8 (drop Cb/Cr, keep Y only)
   │
   ▼
VDMA Write  ──── store frames to DDR (triple buffer)
```

**YUV4:2:2 byte layout (before Mono8 downsampling):**

| Bits | Field | Description |
|---|---|---|
| 0–7 | Pixel 0 – Y | Valid Luma |
| 8–15 | Pixel 0 – Cb/Cr | Valid Chroma (multiplexed) |
| 16–23 | Zero / Padding | Unused in 4:2:2 mode |

---

### VDMA & Frame Synchronisation

Triple-buffer scheme in DDR: Frame 0, Frame 1, Frame 2.

- **Master VDMA** — driven by Sensor A; writes to DDR whenever the camera sends data.
- **Slave VDMA** — driven by Sensor B; receives `frame_ptr` from the Master and forces its buffer index to match, preventing the two sensors from drifting to different indices.
- **Read VDMA** — also slaved to the Master index.
  - Camera faster than display → Read VDMA **skips** a frame to catch up.
  - Camera slower than display → Read VDMA **repeats** a frame.

**VTC timing for 200 fps (40 MHz clock, 4 pixels per clock):**

```
Target Total Pixels = Clock × PPC / Target FPS
                    = 40,000,000 × 4 / 200
                    = 800,000

V Total (frame length) = 557  (from IMX477 register)
H Total = 800,000 / 557 = 1436.26  →  rounded to 1436
```

> For fine H-total adjustment see: https://adaptivesupport.amd.com/s/article/899769

**Hardware synchronisation note:** The IMX477 XVS pin direction cannot be changed from output to input (register sheet not fully public), so XVS is left floating. Instead, synchronisation is achieved by:
1. Length-matching the power traces to both sensors so they power up simultaneously.
2. Triple buffering + a shared crystal so the two sensors share the same clock domain (no software PLL required).

---

### AXI4-Stream to Video Out

This bridge merges pixel data from the VDMA with timing signals from the VTC.

**Preventing Underflow** (screen flashing black / colour shift when VDMA cannot keep up):

- Set the AXI4-Stream Data FIFO depth to **4096 or 8192 words**.
- Set **Hysteresis = 64** in the Video Out IP — the IP will not start a new line until 64 words are ready in the FIFO.
- **Correct startup order:** ensure the camera is writing to DDR *before* enabling the VTC generator.

---

### VDMA Stride — Merging Two Sensors into 1280×480

Both sensors write to the same DDR frame buffer side-by-side. Sensor A writes to the left half; Sensor B is offset by 640 pixels (1280 bytes).

```c
// ── Sensor A ───────────────────────────────────────────────
XAxiVdma_DmaSetup WriteCfg_A;
WriteCfg_A.VertSizeInput = 480;           // frame height
WriteCfg_A.HoriSizeInput = 640 * 2;       // width in bytes (16-bit/px)
WriteCfg_A.Stride        = 1280 * 2;      // full canvas width in bytes
WriteCfg_A.FrameStoreStartAddr[0] = 0x10000000;
XAxiVdma_DmaSetSetup(&VdmaInstance_A, XAXIVDMA_WRITE, &WriteCfg_A);

// ── Sensor B (offset by one image width) ───────────────────
XAxiVdma_DmaSetup WriteCfg_B;
WriteCfg_B.VertSizeInput = 480;
WriteCfg_B.HoriSizeInput = 640 * 2;
WriteCfg_B.Stride        = 1280 * 2;
// 0x10000000 + (640 pixels × 2 bytes/pixel) = 0x10000500
WriteCfg_B.FrameStoreStartAddr[0] = 0x10000000 + (640 * 2);
XAxiVdma_DmaSetSetup(&VdmaInstance_B, XAXIVDMA_WRITE, &WriteCfg_B);
```

---
