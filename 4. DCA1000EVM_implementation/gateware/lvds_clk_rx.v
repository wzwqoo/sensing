//==============================================================================
// lvds_clk_rx.v
// Receives the AWR1843 source-synchronous LVDS clock.
//
// IBUFDS  — differential input buffer (no drive, minimal delay)
// BUFIO   — clock buffer staying in the I/O column → feeds ISERDESE2 CLK
// BUFR    — regional clock buffer, divide by SERDES_RATIO → feeds CLKDIV
//
// Clock-active detector:
//   The AWR1843 only drives the LVDS clock during active ADC sampling.
//   Between chirps the clock is IDLE (held static).
//   We detect this by sampling the raw clock with a free-running system clock.
//   If no toggle is seen for IDLE_THRESH system cycles → clk_active = 0.
//==============================================================================

`timescale 1ns/1ps

module lvds_clk_rx #(
    parameter SERDES_RATIO = 8,    // BUFR divide ratio (must match ISERDESE2)
    parameter IDLE_THRESH  = 32    // system-clock cycles without toggle → idle
)(
    input  wire LVDS_CLK_P,
    input  wire LVDS_CLK_N,
    input  wire rst_n,

    output wire clk_buf,       // BUFIO output → ISERDESE2 CLK port
    output wire clk_div,       // BUFR  output → ISERDESE2 CLKDIV port

    // Clock-active flag (in clk_div domain)
    output reg  clk_active
);

// ============================================================================
// 1. Differential input buffer
// ============================================================================

wire clk_ibuf;

IBUFDS #(
    .DIFF_TERM    ("TRUE"),   // internal 100-ohm termination
    .IOSTANDARD   ("LVDS_25") // 1.8V LVDS — match your bank voltage
) u_ibufds (
    .I  (LVDS_CLK_P),
    .IB (LVDS_CLK_N),
    .O  (clk_ibuf)
);

// ============================================================================
// 2. BUFIO — stays in I/O column, lowest skew to ISERDESE2
// ============================================================================

BUFIO u_bufio (
    .I (clk_ibuf),
    .O (clk_buf)
);

// ============================================================================
// 3. BUFR — divides clock for ISERDESE2 CLKDIV + logic fabric
// ============================================================================

BUFR #(
    .BUFR_DIVIDE (SERDES_RATIO), // "8" for DDR 8:1
    .SIM_DEVICE  ("7SERIES")
) u_bufr (
    .I   (clk_ibuf),
    .CE  (1'b1),
    .CLR (1'b0),
    .O   (clk_div)
);

// ============================================================================
// 4. Clock-active detector
//    Sample clk_ibuf with clk_div (they share the same source, just divided).
//    Count cycles since last detected edge; if > IDLE_THRESH → idle.
//
//    NOTE: clk_ibuf toggles at the LVDS bit rate. In clk_div domain we will
//    always see a non-constant value on clk_ibuf when clock is running,
//    because clk_div is slower by SERDES_RATIO.
//    We use a 2-FF synchronizer + edge detector on clk_ibuf.
// ============================================================================

reg [1:0]             clk_sync_ff;    // synchronizer for clk_ibuf
reg                   clk_last;
reg [$clog2(IDLE_THRESH+1)-1:0] idle_cnt;

always @(posedge clk_div or negedge rst_n) begin
    if (!rst_n) begin
        clk_sync_ff <= 2'b00;
        clk_last    <= 1'b0;
        idle_cnt    <= 0;
        clk_active  <= 1'b0;
    end else begin
        // 2-stage synchronizer
        clk_sync_ff <= {clk_sync_ff[0], clk_ibuf};
        clk_last    <= clk_sync_ff[1];

        if (clk_sync_ff[1] != clk_last) begin
            // Edge detected → clock is running
            idle_cnt   <= 0;
            clk_active <= 1'b1;
        end else begin
            if (idle_cnt < IDLE_THRESH) begin
                idle_cnt <= idle_cnt + 1;
            end else begin
                clk_active <= 1'b0;  // no edge for IDLE_THRESH cycles → idle
            end
        end
    end
end

endmodule
