#ifndef CFAR_DETECTOR_H
#define CFAR_DETECTOR_H

// ============================================================================
//  cfar_detector.h
//  2D CA-CFAR (Cell Averaging) detector — golf ball & club head targets
//
//  Range-Doppler map : 1024 range bins × 128 Doppler bins
//  Interface         : m_axi master (rd_map read + det_map write)
//                      s_axilite slave (alpha_q8 control register + ap_ctrl)
//
//  Golf target RCS context
//  -----------------------
//  Golf ball  : ~-30 to -25 dBsm   small, fast  (~70 m/s max swing speed)
//  Club head  : ~-20 to -15 dBsm   larger RCS, same velocity range
//
//  Algorithm : 2-pass separable CA-CFAR
//    Pass 1 (range axis)   — sliding window sum per Doppler row → noise_map
//    Pass 2 (Doppler axis) — sliding window sum per range column → threshold
//
//  Guard cells stop target energy leaking into the noise estimate.
//  Training cells must span at least one clutter correlation length.
// ============================================================================

#include <ap_int.h>   // ap_uint only — no ap_fixed or hls_stream needed here

// ── Grid dimensions ──────────────────────────────────────────────────────────
static const int RANGE_BINS   = 1024;
static const int DOPPLER_BINS = 128;

// ── CFAR window parameters ───────────────────────────────────────────────────
// Range axis (fast-time)
static const int N_GUARD_R = 2;    // guard cells each side in range
static const int N_TRAIN_R = 16;   // training cells each side in range

// Doppler axis (slow-time)
static const int N_GUARD_D = 1;    // guard cells each side in Doppler
static const int N_TRAIN_D = 8;    // training cells each side in Doppler

// ── Total 2D training cells per CUT ──────────────────────────────────────────
// Full window area minus the guard+CUT box:
//   Range  window width  : 2*(N_GUARD_R + N_TRAIN_R) + 1 = 2*(2+16)+1 = 37
//   Doppler window height: 2*(N_GUARD_D + N_TRAIN_D) + 1 = 2*(1+8)+1  = 19
//   Guard box            : (2*N_GUARD_R + 1) * (2*N_GUARD_D + 1)      = 5×3 = 15
//   N_TRAIN_TOTAL        : 37 × 19 − 15 = 703 − 15 = 688
static const int N_TRAIN_TOTAL =
    (2*(N_GUARD_R + N_TRAIN_R) + 1) * (2*(N_GUARD_D + N_TRAIN_D) + 1)
    - (2*N_GUARD_R + 1) * (2*N_GUARD_D + 1);

// ── CFAR threshold factor α — Q8.8 fixed-point ───────────────────────────────
// Detection rule (avoids division):
//   CUT × N_TRAIN_TOTAL  >  α × col_sum   →  detection
//
// For CA-CFAR, α relates to desired false-alarm rate P_fa via:
//   α = N_TRAIN_TOTAL × ( P_fa^(−1/N_TRAIN_TOTAL) − 1 )
//
// Encoding: alpha_q8 = round(α × 2^8) stored in ap_uint<16>.
//   P_fa = 1e-4  →  α ≈ 12.3  →  alpha_q8 ≈ 3149
//   P_fa = 1e-5  →  α ≈ 19.7  →  alpha_q8 ≈ 5043   (design default)
//   P_fa = 1e-6  →  α ≈ 27.2  →  alpha_q8 ≈ 6963
//
// alpha_q8 is a run-time s_axilite register — P_fa can be tuned per frame
// without re-synthesising the IP.
static const int ALPHA_FRAC = 8;   // Q8.8 fractional bits (scale factor = 256)

// ── Data types ────────────────────────────────────────────────────────────────
//
// cell_t   : one magnitude-squared sample from the range-Doppler map.
//            ap_uint<16> covers a 16-bit ADC power value (0..65535).
//            For an 18-bit ADC widen to ap_uint<20> and recalculate acc_t below.
//
// acc_t    : sliding window accumulator.
//            Worst-case range pass  : 2×N_TRAIN_R cells × 65535 = 2,097,120
//                                     → 21 bits required, ap_uint<32> safe.
//            Worst-case Doppler pass: N_TRAIN_TOTAL × 2,097,120 = 1,442,818,560
//                                     → 31 bits required, ap_uint<32> has 1-bit
//                                     headroom only. If cell_t is widened beyond
//                                     16 bits, widen acc_t to ap_uint<40>.
//
// thresh_t : product (alpha_q8 × col_sum) before the Q8 right-shift.
//            Worst-case: 65535 × 1,442,818,560 = 94,522,897,612,800
//                        → 47 bits required, ap_uint<48> has 1-bit headroom.
//            If acc_t is widened, widen thresh_t to ap_uint<64>.
//
// det_t    : 1-bit detection flag per cell (1 = target detected).

typedef ap_uint<16>  cell_t;     // range-Doppler map input sample
typedef ap_uint<32>  acc_t;      // training window accumulator
typedef ap_uint<48>  thresh_t;   // alpha × noise_sum product
typedef ap_uint<1>   det_t;      // per-cell detection output

// ── Top-level function prototype ──────────────────────────────────────────────
// HLS interfaces (defined by #pragma HLS INTERFACE in cfar_detector.cpp):
//   rd_map    → m_axi master  reads  RD map from DDR3
//   det_map   → m_axi master  writes detection map to DDR3
//   alpha_q8  → s_axilite     run-time threshold register
//   return    → s_axilite     ap_start / ap_done / ap_idle handshake
void cfar_detector(
    cell_t      rd_map [DOPPLER_BINS][RANGE_BINS],
    det_t       det_map[DOPPLER_BINS][RANGE_BINS],
    ap_uint<16> alpha_q8
);

#endif // CFAR_DETECTOR_H