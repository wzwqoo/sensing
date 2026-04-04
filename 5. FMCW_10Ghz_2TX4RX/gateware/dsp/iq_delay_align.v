// =============================================================================
//  iq_delay_align.v
//  Aligns the I (in-phase) channel with Q (Hilbert transform output).
//
//  Background
//  ----------
//  The Vivado FIR Compiler in "Hilbert" mode outputs ONLY the imaginary
//  (Hilbert-transformed) component Q = H{x[n]}.
//  The real component I = x[n] is simply the original input, but delayed by
//  the same HILBERT_LATENCY clock cycles so that I[n] and Q[n] refer to the
//  same input sample.
//
//  This module runs entirely in the TDM domain (fast clock = 4×fs).
//  It takes the raw MUX output (before the Hilbert FIR) as input, delays it
//  by HILBERT_LATENCY, and presents it as i_tdm alongside q_tdm (= Hilbert
//  FIR output).
//
//  After this module, both i_tdm and q_tdm are in the TDM domain (valid
//  every clock), carrying interleaved data for ch0, ch1, ch2, ch3 in order.
//  They feed into two separate tdm_demux_1to4 instances to split back to 4
//  individual slow-rate I and Q channels.
//
//  Memory usage
//  ------------
//  The shift registers (depth = HILBERT_LATENCY) synthesise as:
//    HILBERT_LATENCY <= 16  → SRL16E (1 LUT per bit)
//    16 < HILBERT_LATENCY <= 32 → SRL32  (1 LUT per bit)
//    HILBERT_LATENCY > 32  → RAMB36 or register chain (tool-dependent)
//  For DATA_W=16, HILBERT_LATENCY=58: ~58 × 16 = 928 FFs or ~30 LUTs as SRL.
//  You must read the exact value from the Vivado FIR Compiler synthesis report — do not hardcode 58.
//  If you see channel 0 data appearing on channel 1 output, increment HILBERT_LATENCY by 1. 
// If channel 3 data appears on channel 2, decrement by 1.

//  Parameters
//  ----------
//  DATA_W          : data bus width
//  HILBERT_LATENCY : FIR pipeline depth (fast clock cycles) — must match
//                    the LATENCY reported by Vivado FIR Compiler for the
//                    Hilbert IP, plus 1 for the MUX output register.
// =============================================================================

`default_nettype none

module iq_delay_align #(
    parameter DATA_W          = 16,
    parameter HILBERT_LATENCY = 58
)(
    input  wire             clk,
    input  wire             rst_n,

    // ── Raw TDM stream (after 4:1 MUX, before Hilbert FIR) ───────────────
    input  wire [DATA_W-1:0] raw_tdm_data,
    input  wire              raw_tdm_valid,

    // ── Hilbert FIR output (Q channel, TDM) ──────────────────────────────
    input  wire [DATA_W-1:0] hilbert_data,   // from FIR m_axis_data_tdata
    input  wire              hilbert_valid,  // from FIR m_axis_data_tvalid

    // ── Time-aligned I/Q outputs (both valid at same cycle) ───────────────
    output wire [DATA_W-1:0] i_tdm_data,    // delayed original = I
    output wire              i_tdm_valid,
    output wire [DATA_W-1:0] q_tdm_data,    // Hilbert output = Q
    output wire              q_tdm_valid
);

    // ── I shift register: delay raw input by HILBERT_LATENCY clocks ───────
    // Xilinx tools infer this as SRL16/SRL32 chains automatically.
    // Use (* shreg_extract = "yes" *) attribute to guarantee SRL inference.
    // SRL32 as a "compact' hardware shift register that can hold 32 bits of delay in the space of a single logic gate. 
    // It’s the "pro" way to handle delays in FPGAs.
    (* shreg_extract = "yes" *)
    reg [DATA_W-1:0] i_data_sr [0:HILBERT_LATENCY-1];
    (* shreg_extract = "yes" *)
    reg              i_vld_sr  [0:HILBERT_LATENCY-1];

    integer j;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (j = 0; j < HILBERT_LATENCY; j = j + 1) begin
                i_data_sr[j] <= {DATA_W{1'b0}};
                i_vld_sr[j]  <= 1'b0;
            end
        end else begin
            i_data_sr[0] <= raw_tdm_data;
            i_vld_sr[0]  <= raw_tdm_valid;
            for (j = 1; j < HILBERT_LATENCY; j = j + 1) begin
                i_data_sr[j] <= i_data_sr[j-1];
                i_vld_sr[j]  <= i_vld_sr[j-1];
            end
        end
    end

    // ── I output = tail of delay chain ────────────────────────────────────
    assign i_tdm_data  = i_data_sr[HILBERT_LATENCY-1];
    assign i_tdm_valid = i_vld_sr[HILBERT_LATENCY-1];

    // ── Q output = directly from Hilbert FIR (already correctly delayed) ──
    assign q_tdm_data  = hilbert_data;
    assign q_tdm_valid = hilbert_valid;

    // ── Assertion (simulation only): check I and Q valid are in sync ───────
    // synthesis translate_off
    always @(posedge clk) begin
        if (i_tdm_valid !== q_tdm_valid) begin
            $display("[iq_delay_align] WARNING t=%0t: I valid=%b Q valid=%b — misaligned!",
                     $time, i_tdm_valid, q_tdm_valid);
        end
    end
    // synthesis translate_on

endmodule

`default_nettype wire
