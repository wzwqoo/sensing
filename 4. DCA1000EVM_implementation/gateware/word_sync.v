//==============================================================================
// word_sync.v
// Frame-level word synchronization.
//
// After bit_sync locks all lanes, each lane produces 16-bit words.
// The AWR1843 sends a header word 0xF800 at the start of every chirp packet
// on ALL lanes simultaneously.
//
// This module:
//   1. Watches for 0xF800 on lane 0 (master reference lane)
//   2. Verifies all lanes show the header simultaneously
//   3. Drops the header word from the output (it's not IQ data)
//   4. Asserts out_valid for every subsequent word (actual samples)
//   5. Resets at end of chirp (when clk_active falls)
//
// The header appears ONCE per chirp burst. After header detection,
// all words until clock-idle are IQ samples.
//==============================================================================

`timescale 1ns/1ps

module word_sync #(
    parameter NUM_LANES    = 4,
    parameter SYNC_PATTERN = 16'hF800
)(
    input  wire        clk_div,
    input  wire        rst_n,

    // From bit_sync (all lanes, concatenated: [lane3|lane2|lane1|lane0])
    input  wire [NUM_LANES*16-1:0] lane_words,
    input  wire                    words_in_valid,  // bit_sync all locked

    // Aligned IQ data out (header stripped)
    output reg  [15:0] lane0_out,
    output reg  [15:0] lane1_out,
    output reg  [15:0] lane2_out,
    output reg  [15:0] lane3_out,
    output reg         out_valid,
    output reg         sync_locked
);

// ============================================================================
// Unpack lanes
// ============================================================================

wire [15:0] lane_w [NUM_LANES-1:0];
genvar g;
generate
    for (g = 0; g < NUM_LANES; g = g + 1)
        assign lane_w[g] = lane_words[g*16 +: 16];
endgenerate

// ============================================================================
// Header detection
// ============================================================================

// All lanes must show the sync pattern simultaneously
wire header_on_all;
genvar h;
wire [NUM_LANES-1:0] header_match;
generate
    for (h = 0; h < NUM_LANES; h = h + 1)
        assign header_match[h] = (lane_w[h] == SYNC_PATTERN);
endgenerate
assign header_on_all = &header_match;

// ============================================================================
// State machine
// ============================================================================

localparam ST_WAIT_HEADER = 1'b0;  // waiting for 0xF800 on all lanes
localparam ST_DATA        = 1'b1;  // passing IQ data through

reg state;
reg clk_active_d;   // to detect falling edge of words_in_valid

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        state       <= ST_WAIT_HEADER;
        sync_locked <= 1'b0;
        out_valid   <= 1'b0;
        lane0_out   <= 16'h0;
        lane1_out   <= 16'h0;
        lane2_out   <= 16'h0;
        lane3_out   <= 16'h0;
        clk_active_d <= 1'b0;
    end else begin
        clk_active_d <= words_in_valid;
        out_valid    <= 1'b0;  // default

        case (state)
            // ------------------------------------------------------------------
            ST_WAIT_HEADER: begin
                sync_locked <= 1'b0;
                if (words_in_valid && header_on_all) begin
                    // Header detected: transition to data phase
                    // (header word itself is NOT passed to output)
                    state       <= ST_DATA;
                    sync_locked <= 1'b1;
                end
            end

            // ------------------------------------------------------------------
            ST_DATA: begin
                sync_locked <= 1'b1;

                if (!words_in_valid) begin
                    // Clock went idle → chirp ended, reset for next burst
                    state       <= ST_WAIT_HEADER;
                    sync_locked <= 1'b0;
                    out_valid   <= 1'b0;
                end else if (header_on_all) begin
                    // Another header seen → start of new chirp within frame
                    // Drop this header word, stay in ST_DATA for next chirp
                    out_valid <= 1'b0;
                end else begin
                    // Regular IQ sample word — pass through
                    lane0_out <= lane_w[0];
                    lane1_out <= lane_w[1];
                    lane2_out <= lane_w[2];
                    lane3_out <= lane_w[3];
                    out_valid <= 1'b1;
                end
            end

            default: state <= ST_WAIT_HEADER;
        endcase
    end
end

endmodule
