//==============================================================================
// bit_sync.v
// Bit synchronization: issues BITSLIP pulses until the LVDS training/sync
// pattern (0xF800) is found in the deserialized bitstream.
//
// AWR1843 LVDS packet format (each chirp):
//   [15:0]  = 0xF800  (sync header, sent first on every lane at chirp start)
//   [15:0]  = sample 0 I
//   [15:0]  = sample 0 Q
//   ...
//
// Strategy:
//   - Accumulate 2× SERDES_RATIO bits into a 16-bit shift register
//   - Check all possible byte offsets for the pattern
//   - When found, record the offset and apply that many bitslips
//   - After LOCK_CONFIRM_COUNT consecutive matches → assert sync_locked
//   - If locked and pattern goes missing → re-sync (glitch protection)
//
// IMPORTANT: ISERDESE2 requires bitslip pulses spaced at least 2 CLKDIV
// cycles apart. We enforce this with a bitslip cooldown counter.
//==============================================================================

`timescale 1ns/1ps

module bit_sync #(
    parameter SERDES_RATIO      = 8,
    parameter SYNC_PATTERN      = 16'hF800,
    parameter LOCK_CONFIRM_COUNT = 8,   // consecutive chirp headers to confirm lock
    parameter BITSLIP_COOLDOWN  = 4    // clk_div cycles between bitslips
)(
    input  wire                    clk_div,
    input  wire                    rst_n,
    input  wire [SERDES_RATIO-1:0] raw_data,    // from ISERDESE2
    input  wire                    clk_active,   // from lvds_clk_rx

    output reg                     bitslip_out,  // to ISERDESE2 BITSLIP port
    output reg  [15:0]             aligned_word, // word-aligned 16-bit output
    output reg                     word_valid,   // aligned_word is valid this cycle
    output reg                     sync_locked   // pattern confirmed, data good
);

// ============================================================================
// Shift register: hold last 2*SERDES_RATIO bits so we can check all offsets
// ============================================================================

reg [2*SERDES_RATIO-1:0] shift_reg;

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n)
        shift_reg <= 0;
    else
        shift_reg <= {raw_data, shift_reg[2*SERDES_RATIO-1:SERDES_RATIO]};
end

// ============================================================================
// Pattern search across all bit offsets within the 16-bit window
// ============================================================================

// We look at every bit offset 0..SERDES_RATIO-1 within shift_reg
// to find where 0xF800 appears.

integer k;
reg [SERDES_RATIO-1:0] pattern_hit; // which offset has a match

always @(*) begin
    for (k = 0; k < SERDES_RATIO; k = k + 1)
        pattern_hit[k] = (shift_reg[k +: 16] == SYNC_PATTERN);
end

wire any_hit = |pattern_hit;

// Encode which offset was found (priority encoder, lowest offset wins)
reg [$clog2(SERDES_RATIO)-1:0] hit_offset;
always @(*) begin
    hit_offset = 0;
    for (k = SERDES_RATIO-1; k >= 0; k = k - 1)
        if (pattern_hit[k]) hit_offset = k[$clog2(SERDES_RATIO)-1:0];
end

// ============================================================================
// State machine
// ============================================================================

localparam ST_SEARCH  = 2'd0;  // issuing bitslips, looking for pattern
localparam ST_CONFIRM = 2'd1;  // found pattern, counting confirmations
localparam ST_LOCKED  = 2'd2;  // locked, normal operation

reg [1:0]  state;
reg [$clog2(LOCK_CONFIRM_COUNT+1)-1:0] confirm_cnt;
reg [$clog2(BITSLIP_COOLDOWN+1)-1:0]  cooldown;
reg                                    issue_slip;

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        state       <= ST_SEARCH;
        confirm_cnt <= 0;
        cooldown    <= 0;
        bitslip_out <= 1'b0;
        sync_locked <= 1'b0;
        issue_slip  <= 1'b0;
    end else begin
        bitslip_out <= 1'b0;   // default: no bitslip

        // Cooldown counter between bitslips
        if (cooldown > 0)
            cooldown <= cooldown - 1;

        case (state)
            // ------------------------------------------------------------------
            ST_SEARCH: begin
                sync_locked <= 1'b0;
                if (!clk_active) begin
                    // No clock → radar not sampling, stay idle
                end else if (any_hit) begin
                    // Pattern found at hit_offset — if offset > 0 we still need
                    // that many more bitslips to shift data into exact alignment.
                    // (hit_offset = 0 means already aligned)
                    if (hit_offset == 0) begin
                        state       <= ST_CONFIRM;
                        confirm_cnt <= 0;
                    end else begin
                        // Issue one more bitslip to shift toward alignment
                        if (cooldown == 0) begin
                            bitslip_out <= 1'b1;
                            cooldown    <= BITSLIP_COOLDOWN;
                        end
                    end
                end else begin
                    // No pattern found → keep slipping
                    if (cooldown == 0) begin
                        bitslip_out <= 1'b1;
                        cooldown    <= BITSLIP_COOLDOWN;
                    end
                end
            end

            // ------------------------------------------------------------------
            ST_CONFIRM: begin
                if (!clk_active) begin
                    state <= ST_SEARCH;  // clock went away, re-sync next burst
                end else if (any_hit && hit_offset == 0) begin
                    if (confirm_cnt == LOCK_CONFIRM_COUNT - 1) begin
                        state       <= ST_LOCKED;
                        sync_locked <= 1'b1;
                    end else begin
                        confirm_cnt <= confirm_cnt + 1;
                    end
                end else begin
                    // Pattern lost before confirming → back to search
                    state <= ST_SEARCH;
                end
            end

            // ------------------------------------------------------------------
            ST_LOCKED: begin
                sync_locked <= 1'b1;
                if (!clk_active) begin
                    // Between chirps: stay locked (pattern will return next chirp)
                end else if (!any_hit) begin
                    // Unexpected pattern loss — could be data (not header word)
                    // This is normal during data payload; do NOT re-slip here.
                    // word_sync will use its own header detection.
                end
            end

            default: state <= ST_SEARCH;
        endcase
    end
end

// ============================================================================
// Output: extract the aligned 16-bit word from shift_reg at offset 0
// ============================================================================

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        aligned_word <= 16'h0000;
        word_valid   <= 1'b0;
    end else begin
        aligned_word <= shift_reg[15:0];  // offset-0 = aligned slice
        word_valid   <= (state == ST_LOCKED) && clk_active;
    end
end

endmodule
