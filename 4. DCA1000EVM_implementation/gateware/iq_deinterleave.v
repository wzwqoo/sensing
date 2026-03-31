//==============================================================================
// iq_deinterleave.v
// De-interleaves 4 LVDS lanes into RX0..RX3 I/Q sample pairs.
//
// AWR1843 lane assignment (4 RX, 4 lanes, complex data):
//
//   Lane 0 → RX0  (I and Q interleaved: word0=I, word1=Q, word2=I, ...)
//   Lane 1 → RX1  (I and Q interleaved)
//   Lane 2 → RX2  (I and Q interleaved)
//   Lane 3 → RX3  (I and Q interleaved)
//
// All four lanes carry samples simultaneously. Each lane streams:
//   [I₀][Q₀][I₁][Q₁]...[I_{N-1}][Q_{N-1}]
//
// This module uses a single shared toggle flag (iq_phase) across all lanes.
// All lanes always carry the same phase simultaneously — on even words every
// lane carries I, on odd words every lane carries Q.
//
// The I word is latched into a hold register; when the Q word arrives the
// complete I/Q pair is written to the outputs and out_valid pulses for 1 cycle.
// out_valid therefore fires at half the rate of in_valid (once per complex
// sample, not once per word).
//
// out_valid is suppressed during in_valid low periods (between chirps) and
// the phase toggle is reset so the next chirp always starts on I.
//
// For REAL-only data mode: remove iq_phase logic, output lane directly to _i,
// tie _q to zero, and assert out_valid every in_valid cycle.
//==============================================================================

`timescale 1ns/1ps

module iq_deinterleave (
    input  wire        clk_div,
    input  wire        rst_n,

    // Word-aligned, header-stripped lane data from word_sync
    // Each lane delivers interleaved I/Q: even word = I, odd word = Q
    input  wire [15:0] lane0,   // RX0: alternating I/Q
    input  wire [15:0] lane1,   // RX1: alternating I/Q
    input  wire [15:0] lane2,   // RX2: alternating I/Q
    input  wire [15:0] lane3,   // RX3: alternating I/Q
    input  wire        in_valid, // one pulse per LVDS word (both I and Q words)

    // Registered IQ outputs — valid once per complex sample (every 2 in_valid)
    output reg  [15:0] rx0_i,
    output reg  [15:0] rx0_q,
    output reg  [15:0] rx1_i,
    output reg  [15:0] rx1_q,
    output reg  [15:0] rx2_i,
    output reg  [15:0] rx2_q,
    output reg  [15:0] rx3_i,
    output reg  [15:0] rx3_q,
    output reg         out_valid  // 1-cycle pulse per complete I/Q sample set
);

// ============================================================================
// I-phase hold registers (capture I word while waiting for Q)
// ============================================================================

reg [15:0] hold0_i, hold1_i, hold2_i, hold3_i;

// ============================================================================
// Phase toggle: 0 = current word is I, 1 = current word is Q
// Shared across all lanes — they are always in sync.
// Resets to 0 (I-phase) whenever in_valid deasserts (chirp boundary).
// ============================================================================

reg iq_phase;  // 0=I, 1=Q

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        iq_phase  <= 1'b0;
        hold0_i   <= 16'h0;
        hold1_i   <= 16'h0;
        hold2_i   <= 16'h0;
        hold3_i   <= 16'h0;
        rx0_i     <= 16'h0;  rx0_q <= 16'h0;
        rx1_i     <= 16'h0;  rx1_q <= 16'h0;
        rx2_i     <= 16'h0;  rx2_q <= 16'h0;
        rx3_i     <= 16'h0;  rx3_q <= 16'h0;
        out_valid <= 1'b0;
    end else begin
        out_valid <= 1'b0;  // default: no output this cycle

        if (!in_valid) begin
            // Between chirps: reset phase so next chirp starts on I
            iq_phase <= 1'b0;
        end else begin
            if (iq_phase == 1'b0) begin
                // ── I word: latch into hold registers, do not output yet ──
                hold0_i  <= lane0;
                hold1_i  <= lane1;
                hold2_i  <= lane2;
                hold3_i  <= lane3;
                iq_phase <= 1'b1;
            end else begin
                // ── Q word: complete the pair, write outputs, pulse valid ──
                rx0_i     <= hold0_i;   rx0_q <= lane0;
                rx1_i     <= hold1_i;   rx1_q <= lane1;
                rx2_i     <= hold2_i;   rx2_q <= lane2;
                rx3_i     <= hold3_i;   rx3_q <= lane3;
                out_valid <= 1'b1;
                iq_phase  <= 1'b0;
            end
        end
    end
end

endmodule
