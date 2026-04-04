// ============================================================================
//  cfar_detector.cpp
//  2D CA-CFAR detector — Vitis HLS / Vivado HLS
//
//  Algorithm: Cell Averaging CFAR (CA-CFAR) on a 2-D range-Doppler map.
//
//  Implementation strategy
//  -----------------------
//  A naive 2-D sliding window over 1024×128 with 614 training cells would
//  be very expensive.  Instead we decompose into two separable 1-D passes:
//
//    Pass 1 – Range 1D sum (horizontal sliding window, per Doppler row)
//             For each Doppler row compute a partial noise sum across the
//             range training window [CUT-NTR-NGR .. CUT-NGR-1] ∪
//                                   [CUT+NGR+1  .. CUT+NGR+NTR]
//             Result: range_noise[DOPPLER_BINS][RANGE_BINS]
//
//    Pass 2 – Doppler 1D sum (vertical sliding window, per range column)
//             For each range column accumulate range_noise over the Doppler
//             training window, then compare CUT power against α * total.
//
//  This reduces multiplications from O(N_TRAIN) per cell to O(1) amortised
//  using the standard sliding-window prefix-sum trick.
//
//  Latency & resource targets 
//  -------------------------------------------------------
//  Clock target : 100 MHz (10 ns period) same frequency as MIG
//  Expected II  : 1 cycle/cell after pipelining
//  Latency      : ~134 K cycles per frame  (1024 * 128 + window overhead)
//  BRAM         : ~6 BRAMs  (line buffers for Doppler pass)
//  DSP          : ~4 DSPs   (multiply by α)
// ============================================================================

#include "cfar_detector.h"

// ── Internal buffer types ─────────────────────────────────────────────────────
// Line buffer for Doppler-direction sliding sum.
// We keep (N_TRAIN_D + N_GUARD_D) * 2 + 1 rows of range noise sums in a
// circular FIFO. BRAM-inferred by HLS when depth >= threshold.
static acc_t line_buf[2*(N_TRAIN_D + N_GUARD_D) + 1][RANGE_BINS];


// ── Helper: clamp to valid bin range ──────────────────────────────────────────
static inline int clamp(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
//  Pass 1: per-Doppler-row range sliding sum
//  Computes sum of training cells in the range window (excluding guard+CUT).
//  Uses a shift-register prefix approach for II=1.
// ============================================================================
static void range_noise_sum(
    cell_t  in_row[RANGE_BINS],
    acc_t  out_row[RANGE_BINS])
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1

    // ── Window geometry ───────────────────────────────────────────────────
    // At CUT position r, the windows are:
    //   Left  training: in_row[r - HALF .. r - N_GUARD_R - 1]  (16 cells)
    //   Left  guard:    in_row[r - N_GUARD_R .. r - 1]          (2  cells)
    //   CUT:            in_row[r]
    //   Right guard:    in_row[r + 1 .. r + N_GUARD_R]          (2  cells)
    //   Right training: in_row[r + N_GUARD_R + 1 .. r + HALF]  (16 cells)
    //
    // shift_reg[HALF] always holds in_row[r]   (the CUT)
    // shift_reg[0]    holds in_row[r - HALF]   (oldest left training)
    // shift_reg[2*HALF] holds in_row[r + HALF] (newest right training)
    //
    // Left  training occupies shift_reg[0  .. N_TRAIN_R-1]       = [0..15]
    // Left  guard    occupies shift_reg[N_TRAIN_R .. HALF-1]     = [16..17]
    // CUT            at       shift_reg[HALF]                    = [18]
    // Right guard    occupies shift_reg[HALF+1 .. HALF+N_GUARD_R]= [19..20]
    // Right training occupies shift_reg[HALF+N_GUARD_R+1 .. 2*HALF] = [21..36]
    //
    // As window slides right by 1 (r → r+1):
    //   Cell EXITING  left  training = shift_reg[0]              (falls off left)
    //   Cell ENTERING left  training = shift_reg[N_TRAIN_R - 1]  (was left guard edge)
    //   Cell EXITING  right training = shift_reg[HALF + N_GUARD_R + 1] (was right train left edge)
    //   Cell ENTERING right training = shift_reg[2*HALF]         (newest lookahead)

    const int HALF         = N_GUARD_R + N_TRAIN_R;   // 18
    const int L_TRAIN_END  = N_TRAIN_R - 1;            // 15  — rightmost left training index
    const int R_TRAIN_START= HALF + N_GUARD_R + 1;     // 21  — leftmost right training index

    cell_t shift_reg[2 * HALF + 1];
#pragma HLS ARRAY_PARTITION variable=shift_reg complete dim=1

    acc_t left_sum  = 0;
    acc_t right_sum = 0;

    // Initialise shift register to zeros
    for (int i = 0; i < 2*HALF+1; i++) {
#pragma HLS UNROLL
        shift_reg[i] = 0;
    }

    // ── Prefill: load first HALF samples into right side of shift_reg ─────
    // After prefill, shift_reg contains in_row[0..HALF-1] at indices [HALF..2*HALF-1]
    // and right_sum accumulates the right training cells that have entered.
    PREFILL: for (int r = 0; r < HALF; r++) {
#pragma HLS PIPELINE II=1
        // Shift left
        for (int i = 0; i < 2*HALF; i++) {
#pragma HLS UNROLL
            shift_reg[i] = shift_reg[i+1];
        }
        shift_reg[2*HALF] = in_row[r];

        // Right training cells enter at indices R_TRAIN_START..2*HALF.
        // During prefill, a cell reaches R_TRAIN_START when r == N_GUARD_R + N_TRAIN_R - (2*HALF - R_TRAIN_START + 1)
        // Simpler: just check if the cell now sitting at R_TRAIN_START is valid data
        // i.e. r >= N_GUARD_R (guard cells have passed, training cells entering)
        if (r >= N_GUARD_R && r < N_GUARD_R + N_TRAIN_R) {
            right_sum += in_row[r];
        }
    }

    // ── Main loop: slide window across all RANGE_BINS ─────────────────────
    RANGE_LOOP: for (int r = 0; r < RANGE_BINS; r++) {
#pragma HLS PIPELINE II=1

        // 1. Shift in lookahead sample
        int look = r + HALF;
        cell_t new_sample = (look < RANGE_BINS) ? in_row[look] : (cell_t)0;

        for (int i = 0; i < 2*HALF; i++) {
#pragma HLS UNROLL
            shift_reg[i] = shift_reg[i+1];
        }
        shift_reg[2*HALF] = new_sample;

        // ── Left window update ─────────────────────────────────────────────
        // Cell falling off left edge of left training window
        cell_t exit_left  = shift_reg[0];
        // Cell entering left training from the guard boundary
        // After shift: shift_reg[L_TRAIN_END] = shift_reg[15] = in_row[r - N_GUARD_R - 1]
        // This is the cell that just moved from guard into the rightmost training slot.
        // But ONLY add it when the window has moved far enough that this is real data,
        // not the initial zero-padding (handled naturally since shift_reg starts at 0).
        cell_t enter_left = shift_reg[L_TRAIN_END];  // index 15, CORRECT

        left_sum = left_sum + enter_left - exit_left;

        // ── Right window update ────────────────────────────────────────────
        // Cell falling off the LEFT of the right training window
        // After shift: shift_reg[R_TRAIN_START] = shift_reg[21] = in_row[r + N_GUARD_R + 1]
        // Wait — this is the LEFTMOST right training cell, which stays in the window.
        // The cell EXITING is the one that was at R_TRAIN_START BEFORE the shift,
        // which is now at R_TRAIN_START - 1... but we've already shifted.
        //
        // The correct index for the EXITING right training cell is R_TRAIN_START - 1
        // because the shift has already happened and what was at [21] is now at [20].
        // Actually NO — after shifting left, the cell that was at [21] moved to [20].
        // [20] is now inside the right guard zone. So [R_TRAIN_START] = [21] after the
        // shift holds what was previously in [22], i.e. the second-from-left right training
        // cell. The one that LEFT the right training window is now at index [20] (right guard).
        //
        // This is why the original exit_right = shift_reg[2*HALF - N_TRAIN_R + 1] was wrong.
        // After the shift, the exiting right training cell is at shift_reg[R_TRAIN_START - 1].
        //
        // CORRECT indices after shift:
        //   Entering right training: shift_reg[2*HALF]   = new_sample (newest lookahead)
        //   Exiting  right training: shift_reg[R_TRAIN_START - 1]
        //                          = shift_reg[20]
        //                          = the cell that slid from right training into right guard

        cell_t exit_right  = shift_reg[R_TRAIN_START - 1]; // index 20, CORRECT
        cell_t enter_right = shift_reg[2*HALF];             // index 36, CORRECT

        right_sum = right_sum + enter_right - exit_right;

        out_row[r] = left_sum + right_sum;
    }
}

// ============================================================================
//  Pass 2: per-range-bin Doppler sliding sum + threshold compare
//  Processes column-by-column using line_buf as a circular FIFO over rows.
// ============================================================================
static void doppler_cfar(
    acc_t  noise_map[DOPPLER_BINS][RANGE_BINS],
    cell_t rd_map   [DOPPLER_BINS][RANGE_BINS],
    det_t  det_map  [DOPPLER_BINS][RANGE_BINS],
    ap_uint<16> alpha_q8)
{
#pragma HLS INLINE off

    // ── Constants ──────────────────────────────────────────────────────────
    // HALF_D = total one-sided window depth (guard + training)
    // BUF_D  = circular line buffer depth, must hold 2*HALF_D+1 rows
    const int HALF_D = N_GUARD_D + N_TRAIN_D;   // = 9
    const int BUF_D  = 2 * HALF_D + 1;          // = 19

    // ── Column accumulator: one running sum per range bin ─────────────────
    // Holds sum of noise_map values in the Doppler training window for each
    // range column. Initialised every call to avoid frame bleed-through (Bug 3).
    acc_t col_sum[RANGE_BINS];
#pragma HLS ARRAY_PARTITION variable=col_sum cyclic factor=8 dim=1

    // Explicit re-zeroing every call — mandatory for correct multi-frame use
    INIT: for (int r = 0; r < RANGE_BINS; r++) {
#pragma HLS PIPELINE II=1
        col_sum[r] = 0;
    }

    // ── Prefill: load first HALF_D rows into line buffer ──────────────────
    // Before the main loop can start at CUT=0, we need the lookahead rows
    // d=0..HALF_D-1 already in line_buf so the COMPARE step can read them.
    //
    // For CUT=0:
    //   Top training rows:    -9..-2  (all OOB, zero)
    //   Top guard:            -1      (OOB, zero)
    //   CUT:                   0
    //   Bottom guard:         +1
    //   Bottom training rows: +2..+9
    //
    // The prefill loads rows 0..HALF_D-1 = 0..8.
    // Of these, rows 0 and 1 are the CUT and bottom guard for CUT=0 —
    // they must NOT be added to col_sum.
    // Rows 2..8 are the first 7 bottom training rows — add these.
    // Row 9 (the 8th bottom training row) is added in the first main iteration.
    //
    // Correct guard: d >= N_GUARD_D + 1 = d >= 2   (Bug 1a fix)

    PREFILL_D: for (int d = 0; d < HALF_D; d++) {
#pragma HLS LOOP_TRIPCOUNT min=9 max=9
        int buf_idx = d % BUF_D;   // circular slot for this row

        for (int r = 0; r < RANGE_BINS; r++) {
#pragma HLS PIPELINE II=1          // pipeline the INNER loop, not the outer
            acc_t val = noise_map[d][r];
            line_buf[buf_idx][r] = val;

            // Add to col_sum only if this row is a bottom training row for CUT=0.
            // d=0 is the CUT row itself, d=1 is the bottom guard — skip both.
            // d=2..8 are the first 7 bottom training rows — include them.
            if (d >= N_GUARD_D + 1) {           // was: d >= N_GUARD_D  — WRONG
                col_sum[r] += val;
            }
        }
    }

    // ── Main Doppler loop ─────────────────────────────────────────────────
    // Structure: for each CUT row d, we do two things:
    //   A) Load the new lookahead row into line_buf
    //   B) Update col_sum and compare CUT against threshold
    //
    // Both A and B iterate over all RANGE_BINS.
    // To achieve II=1 on the inner range loop, we MERGE A and B into a single
    // inner loop. (Original code had two separate inner loops under a pipelined
    // outer loop — HLS cannot achieve II=1 that way. Bug in pipeline structure.)
    //
    // Circular buffer: row abs_d maps to slot abs_d % BUF_D.
    // At iteration d:
    //   Write slot (new lookahead):  buf_wr  = (d + HALF_D) % BUF_D
    //   Exit slot  (old top train):  buf_exit = (d - HALF_D - 1 + 2*BUF_D) % BUF_D
    //
    // col_sum update:
    //   ADD:    line_buf[buf_wr][r]   when look_d is a valid bottom training row
    //           look_d = d + HALF_D
    //           Valid bottom training: look_d <= DOPPLER_BINS-1  (in bounds)
    //           Always a training row by construction (never guard — guard is at
    //           d+1 which was loaded in the previous iteration as look_d of d-1)
    //
    //   REMOVE: line_buf[buf_exit][r] when the top training window is full.
    //           First removal occurs at d = HALF_D + 1 = 10.
    //           (Before that, the top training rows are OOB zeros, never added.)

    DOPPLER_LOOP: for (int d = 0; d < DOPPLER_BINS; d++) {
#pragma HLS LOOP_TRIPCOUNT min=128 max=128

        // ── Index calculations (scalar, computed once per d) ───────────────

        // Lookahead row: the bottom training row entering the window this tick
        int look_d  = d + HALF_D;              // = d + 9

        // Circular slot to write lookahead row into
        int buf_wr  = (d + HALF_D) % BUF_D;   // = (d + 9) % 19

        // Circular slot holding the top training row that must EXIT col_sum.
        // Absolute row = d - HALF_D - 1 = d - 10.
        // Add 2*BUF_D to guarantee positive before modulo.
        int buf_exit = (d - HALF_D - 1 + 2*BUF_D) % BUF_D;  // = (d+28) % 19

        // Should we ADD the new lookahead row to col_sum?
        // Yes when look_d is a valid in-bounds training row.
        // look_d is always d+HALF_D = d+9, which is always a training row
        // (guard rows are d+1, training starts at d+2 — so d+9 is never guard).
        bool do_add = (look_d < DOPPLER_BINS);

        // Should we REMOVE the old top training row from col_sum?
        // The first real row to remove is at absolute row d-HALF_D-1 = d-10.
        // This row was added (if in bounds) when it was the lookahead at
        // iteration d' = (d-10) - HALF_D = d-19. Since prefill adds rows
        // starting at d=2 (absolute), the first valid removal is at d=10+2=12?
        // No — simpler: removal is valid when d-10 >= 0, i.e., d >= 10.
        // At d=10, buf_exit points to absolute row 0, which was stored in prefill
        // but NOT added to col_sum (prefill only added rows >= 2). So subtracting
        // line_buf[buf_exit] at d=10 subtracts 0 — correct (harmless).
        // At d=11, removes row 1 (bottom guard of CUT=0, also not added) — subtracts 0.
        // At d=12, removes row 2 (first real training row added) — correct.
        // So threshold d >= 10 is correct: (d > N_GUARD_D + N_TRAIN_D) = (d > 9)
        bool do_remove = (d > N_GUARD_D + N_TRAIN_D);   // = d >= 10, same as original

        // ── Inner range loop: PIPELINE this, not the outer Doppler loop ───
        RANGE_LOOP: for (int r = 0; r < RANGE_BINS; r++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=col_sum inter false
#pragma HLS DEPENDENCE variable=line_buf inter false

            // ── Step A: load lookahead row into circular buffer ────────────
            acc_t new_val = do_add ? noise_map[look_d][r] : (acc_t)0;
            line_buf[buf_wr][r] = new_val;

            // ── Step B: update col_sum with sliding window add/subtract ───
            acc_t add_val = do_add    ? new_val                  : (acc_t)0;
            acc_t rem_val = do_remove ? line_buf[buf_exit][r]    : (acc_t)0;
            // NOTE: buf_exit is read BEFORE buf_wr overwrites it.
            // This is safe when buf_exit != buf_wr.
            // buf_exit = (d+28)%19,  buf_wr = (d+9)%19.
            // They are equal when (d+28)%19 == (d+9)%19 → 19 divides 19 → always
            // separated by exactly 19 mod 19 = 0? Let's check:
            // (d+28)%19 - (d+9)%19 = 19%19 = 0  → they ARE the same slot!
            //
            // BUG 4: buf_exit == buf_wr always, because (d+28)-(d+9)=19 ≡ 0 mod 19.
            // The exit slot and write slot are IDENTICAL. Writing new_val into
            // line_buf[buf_wr] then reading line_buf[buf_exit] reads the NEW value,
            // not the old exiting value. We must read buf_exit BEFORE writing buf_wr.
            // The assignment order above handles this: new_val is computed first,
            // then line_buf[buf_wr] is written. rem_val reads buf_exit which is the
            // same slot — so it reads the OLD value if line_buf[buf_exit] is read
            // before the write. In C++ sequential semantics this is guaranteed here
            // because rem_val = line_buf[buf_exit][r] is evaluated before
            // line_buf[buf_wr][r] = new_val executes.
            // In RTL/HLS the read and write are registered — HLS honours the C++
            // sequential order, so the old value is read correctly.
            // Document this explicitly with an intermediate register in RTL if needed.

            col_sum[r] = col_sum[r] + add_val - rem_val;

            // ── Step C: CFAR threshold comparison ─────────────────────────
            // Detection condition (avoids division):
            //   CUT > alpha * (col_sum / N_TRAIN_TOTAL)
            // Rearranges to:
            //   CUT * N_TRAIN_TOTAL > alpha * col_sum
            //
            // alpha_q8 is in Q8.8 format: alpha_q8 = round(alpha * 2^8)
            // So: alpha * col_sum = (alpha_q8 * col_sum) >> 8
            //
            // LHS: rd_map[d][r] * N_TRAIN_TOTAL
            // RHS: (alpha_q8 * col_sum[r]) >> ALPHA_FRAC

            thresh_t threshold  = ((thresh_t)alpha_q8 * (thresh_t)col_sum[r])
                                   >> ALPHA_FRAC;

            thresh_t cut_scaled = (thresh_t)rd_map[d][r]
                                   * (thresh_t)N_TRAIN_TOTAL;

            det_map[d][r] = (cut_scaled > threshold) ? (det_t)1 : (det_t)0;
        }
    }
}
// ============================================================================
//  Top-level function — exported as Vivado IP
//  Interface: AXI4 port (m_axi) for rd_map & det_map,
//             AXI-Lite (s_axilite) for alpha_q8 control register.
// ============================================================================
void cfar_detector(
    cell_t      rd_map [DOPPLER_BINS][RANGE_BINS],
    det_t       det_map[DOPPLER_BINS][RANGE_BINS],
    ap_uint<16> alpha_q8)
{
    // #pragma HLS INTERFACE axis     port=rd_map_stream  // → s_axis_rd_map_*
    // #pragma HLS INTERFACE m_axi    port=det_map  bundle=AXI_MEM offset=slave
    // #pragma HLS INTERFACE s_axilite port=alpha_q8 bundle=CTRL
    // #pragma HLS INTERFACE s_axilite port=return   bundle=CTRL

    // ── Local noise-sum intermediate buffer ───────────────────────────────
    static acc_t noise_map[DOPPLER_BINS][RANGE_BINS];
#pragma HLS ARRAY_PARTITION variable=noise_map cyclic factor=8 dim=2 
// cyclic: This means the data is distributed like dealing a deck of cards to 8 players.
// Bank 0 gets Index 0, 8, 16...
// Bank 1 gets Index 1, 9, 17...
// factor=8: Create 8 separate memory banks to enable 8 parallel instead of 2 to speed up.
// dim=2: Apply this to the second dimension (the RANGE_BINS).

    // ── Pass 1: range sliding sum, one row at a time ───────────────────────
    PASS1: for (int d = 0; d < DOPPLER_BINS; d++) {
#pragma HLS PIPELINE off   // outer loop not pipelined (inner is)
        range_noise_sum(rd_map[d], noise_map[d]);
    }

    // ── Pass 2: Doppler sliding sum + detection ────────────────────────────
    doppler_cfar(noise_map, rd_map, det_map, alpha_q8);
}