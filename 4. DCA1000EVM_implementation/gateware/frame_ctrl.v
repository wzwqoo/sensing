//==============================================================================
// frame_ctrl.v
// Frame / chirp boundary controller.
//
// The AWR1843 drives the LVDS clock ONLY during ADC sampling.
// Between chirps the clock is idle → clk_active transitions signal boundaries.
//
// This module generates:
//   frame_start : 1-cycle pulse at the rising edge of clk_active
//                 = start of new chirp IQ data
//   frame_end   : 1-cycle pulse when word counter reaches SAMPLES_PER_CHIRP*2
//                 or at falling edge of clk_active (whichever comes first)
//
// IMPORTANT — word count vs sample count:
//   With complex (I+Q interleaved) data, each lane sends 2 words per complex
//   sample (one I word + one Q word). So the total LVDS word count per chirp
//   per lane is SAMPLES_PER_CHIRP × 2.
//   words_valid from word_sync pulses once per LVDS word (every clk_div cycle
//   during active data), so we compare against SAMPLES_PER_CHIRP * 2.
//
// SAMPLES_PER_CHIRP must match numAdcSamples in your mmWave profile.
//==============================================================================

`timescale 1ns/1ps

module frame_ctrl #(
    parameter SAMPLES_PER_CHIRP = 256,  // complex samples (I/Q pairs) per chirp
    parameter NUM_LANES         = 4
)(
    input  wire clk_div,
    input  wire rst_n,

    input  wire clk_active,   // from lvds_clk_rx
    input  wire words_valid,  // from word_sync (one pulse per LVDS word, I or Q)

    output reg  frame_start,  // 1-cycle pulse: chirp begins
    output reg  frame_end     // 1-cycle pulse: chirp ends
);

// Words per chirp per lane = complex samples × 2 (one I word + one Q word)
localparam WORDS_PER_CHIRP = SAMPLES_PER_CHIRP * 2;

// ============================================================================
// Edge detection on clk_active
// ============================================================================

reg clk_active_d;
wire rising_edge  = clk_active  && !clk_active_d;
wire falling_edge = !clk_active && clk_active_d;

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) clk_active_d <= 1'b0;
    else        clk_active_d <= clk_active;
end

// ============================================================================
// Word counter — counts LVDS words (each complex sample = 2 counts)
// ============================================================================

reg [$clog2(WORDS_PER_CHIRP+1)-1:0] word_cnt;
wire cnt_done = (word_cnt == WORDS_PER_CHIRP);

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        word_cnt <= 0;
    end else begin
        if (rising_edge) begin
            word_cnt <= 0;                     // reset at chirp start
        end else if (words_valid && !cnt_done) begin
            word_cnt <= word_cnt + 1;
        end
    end
end

// ============================================================================
// SOF / EOF pulse generation
// ============================================================================

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        frame_start <= 1'b0;
        frame_end   <= 1'b0;
    end else begin
        // SOF: rising edge of clk_active = chirp starting
        frame_start <= rising_edge;

        // EOF: word counter reached WORDS_PER_CHIRP, or clock went idle early
        frame_end <= (words_valid && cnt_done) || falling_edge;
    end
end

endmodule
