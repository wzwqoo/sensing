//==============================================================================
// lvds_lane_rx.v
// Single LVDS data lane receiver.
//
// IBUFDS    — differential input buffer
// ISERDESE2 — 8:1 deserializer (DDR mode, NETWORKING interface type)
//
// ISERDESE2 interface type "NETWORKING":
//   Q1..Q8 are the deserialized bits, Q1 = oldest bit received.
//   For DDR with ratio 8: captures 4 DDR bit-periods = 8 bits per CLKDIV cycle.
//
// The bitslip input is a single-cycle pulse that shifts the bit boundary
// by one position. Asserting it repeatedly rotates until sync pattern found.
//==============================================================================

`timescale 1ns/1ps

module lvds_lane_rx #(
    parameter SERDES_RATIO = 8   // must match BUFR divide and ISERDESE2 DATA_WIDTH
)(
    input  wire data_p,
    input  wire data_n,
    input  wire clk,       // BUFIO clock (full-rate LVDS bit clock)
    input  wire clk_div,   // BUFR clock  (clk / SERDES_RATIO)
    input  wire rst_n,

    input  wire bitslip,   // pulse high for 1 clk_div cycle to shift by 1 bit

    // Deserialized output — SERDES_RATIO bits per clk_div cycle
    // Q1 (LSB of data_out) = oldest received bit
    output wire [SERDES_RATIO-1:0] data_out
);

// ============================================================================
// 1. Differential input buffer
// ============================================================================

wire data_ibuf;

IBUFDS #(
    .DIFF_TERM  ("TRUE"),
    .IOSTANDARD ("LVDS_25")
) u_ibufds (
    .I  (data_p),
    .IB (data_n),
    .O  (data_ibuf)
);

// ============================================================================
// 2. ISERDESE2 — 8:1 DDR deserializer
//
//  DATA_RATE    = "DDR"         AWR1843 uses DDR LVDS
//  DATA_WIDTH   = 8             8 bits per CLKDIV cycle
//  INTERFACE_TYPE = "NETWORKING" gives us Q1..Q8 in clk_div domain
//  IOBDELAY     = "NONE"        add IDELAY2 here if you need fine alignment
// ============================================================================

wire [7:0] q_bits;   // Q1..Q8 from ISERDESE2

ISERDESE2 #(
    .DATA_RATE         ("DDR"),
    .DATA_WIDTH        (8),
    .INTERFACE_TYPE    ("NETWORKING"),
    .IOBDELAY          ("NONE"),
    .NUM_CE            (1),
    .OFB_USED          ("FALSE"),
    .SERDES_MODE       ("MASTER"),
    .DYN_CLKDIV_INV_EN ("FALSE"),
    .DYN_CLK_INV_EN    ("FALSE"),
    .INIT_Q1           (1'b0),
    .INIT_Q2           (1'b0),
    .INIT_Q3           (1'b0),
    .INIT_Q4           (1'b0),
    .SRVAL_Q1          (1'b0),
    .SRVAL_Q2          (1'b0),
    .SRVAL_Q3          (1'b0),
    .SRVAL_Q4          (1'b0)
) u_iserdes (
    // Clocks
    .CLK      (clk),        // full-rate bit clock from BUFIO
    .CLKB     (~clk),       // inverted clock for DDR (tie to ~CLK in NETWORKING mode)
    .CLKDIV   (clk_div),    // divided clock from BUFR
    .OCLK     (1'b0),       // not used in NETWORKING mode
    .OCLKB    (1'b0),

    // Data in
    .D        (data_ibuf),  // from IBUFDS
    .DDLY     (1'b0),       // from IDELAY2 if used

    // Bitslip
    .BITSLIP  (bitslip),    // 1-cycle pulse shifts boundary by 1 bit

    // Control
    .CE1      (1'b1),
    .CE2      (1'b1),
    .RST      (~rst_n),     // sync reset, active high for ISERDESE2

    // Deserialized outputs (Q1 = first received)
    .Q1       (q_bits[0]),
    .Q2       (q_bits[1]),
    .Q3       (q_bits[2]),
    .Q4       (q_bits[3]),
    .Q5       (q_bits[4]),
    .Q6       (q_bits[5]),
    .Q7       (q_bits[6]),
    .Q8       (q_bits[7]),

    // Unused
    .O        (),
    .SHIFTOUT1(),
    .SHIFTOUT2(),
    .SHIFTIN1 (1'b0),
    .SHIFTIN2 (1'b0),
    .OFB      (1'b0),
    .DYNCLKDIVSEL(1'b0),
    .DYNCLKSEL   (1'b0)
);

assign data_out = q_bits;

endmodule
