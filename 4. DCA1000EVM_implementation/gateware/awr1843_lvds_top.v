//==============================================================================
// awr1843_lvds_top.v
// Top-level: AWR1843BOOST → Artix-7 LVDS IQ Capture
//
// Instantiates:
//   - lvds_clk_rx     : IBUFDS + BUFIO + BUFR for source-sync clock
//   - lvds_lane_rx    : IBUFDS + ISERDESE2 per data lane (x4 lanes)
//   - bit_sync        : bitslip controller (finds training pattern)
//   - word_sync       : finds 0xF800 header → locks word boundary
//   - frame_ctrl      : detects clock-active/idle, counts samples, SOF/EOF
//   - iq_deinterleave : splits interleaved I/Q per lane → 4 RX I/Q pairs
//   - output_fifo     : async FIFO to user clock domain
//
// AWR1843: 3TX / 4RX, 4 data lanes (1 per RX), Complex (I+Q interleaved), DDR
// Lane N → RX N → words alternate: I₀, Q₀, I₁, Q₁, ...
//==============================================================================

`timescale 1ns/1ps

module awr1843_lvds_top #(
    parameter NUM_LANES        = 4,       // total LVDS data lanes (= num active RX)
    parameter SAMPLES_PER_CHIRP = 256,   // numAdcSamples (complex samples) from mmWave config
    parameter SERDES_RATIO     = 8,       // bits per ISERDESE2 output word (DDR 8:1)
    parameter SYNC_PATTERN     = 16'hF800 // AWR1843 LVDS header sync word
)(
    // -------------------------------------------------------------------------
    // LVDS pins (from AWR1843 J6 connector)
    // -------------------------------------------------------------------------
    input  wire        LVDS_CLK_P,
    input  wire        LVDS_CLK_N,
    input  wire [NUM_LANES-1:0] LVDS_DATA_P,
    input  wire [NUM_LANES-1:0] LVDS_DATA_N,

    // -------------------------------------------------------------------------
    // User / system side
    // -------------------------------------------------------------------------
    input  wire        user_clk,          // application clock (e.g. 100 MHz)
    input  wire        rst_n,             // active-low async reset

    // IQ output — 4 RX, in user_clk domain (from async FIFO)
    // out_valid fires once per complete complex sample set (all 4 RX together)
    output wire [15:0] rx0_i_data,
    output wire [15:0] rx0_q_data,
    output wire [15:0] rx1_i_data,
    output wire [15:0] rx1_q_data,
    output wire [15:0] rx2_i_data,
    output wire [15:0] rx2_q_data,
    output wire [15:0] rx3_i_data,
    output wire [15:0] rx3_q_data,
    output wire        iq_valid,           // 1-cycle pulse: all 4 RX I/Q outputs valid
    output wire        frame_start,        // 1-cycle pulse at chirp start
    output wire        frame_end,          // 1-cycle pulse at chirp end

    // Status / debug
    output wire        bit_sync_locked,
    output wire        word_sync_locked,
    output wire        lvds_clk_active
);

// ============================================================================
// Internal wires
// ============================================================================

wire        lvds_clk_buf;      // buffered LVDS clock (BUFIO, stays in I/O col)
wire        lvds_div_clk;      // divided clock for ISERDESE2 CLK/CLKDIV (BUFR)

wire [SERDES_RATIO-1:0] raw_lane [NUM_LANES-1:0]; // raw ISERDES output per lane
wire        bitslip [NUM_LANES-1:0];               // bitslip strobe per lane

wire [15:0] aligned_word [NUM_LANES-1:0];  // word-aligned 16-bit words
wire        word_valid   [NUM_LANES-1:0];

wire [15:0] lane0_word, lane1_word, lane2_word, lane3_word;
wire        words_valid;

wire [15:0] rx0_i, rx0_q, rx1_i, rx1_q, rx2_i, rx2_q, rx3_i, rx3_q;
wire        iq_valid_int;
wire        frame_start_int, frame_end_int;

// ============================================================================
// 1. LVDS Clock Receiver
// ============================================================================

lvds_clk_rx u_clk_rx (
    .clk_p        (LVDS_CLK_P),
    .clk_n        (LVDS_CLK_N),
    .rst_n        (rst_n),
    .clk_buf      (lvds_clk_buf),   // → BUFIO (feeds ISERDESE2 CLK)
    .clk_div      (lvds_div_clk),   // → BUFR  (feeds ISERDESE2 CLKDIV)
    .clk_active   (lvds_clk_active) // frame boundary detector
);

// ============================================================================
// 2. LVDS Data Lane Receivers (one per lane)
// ============================================================================

genvar i;
generate
    for (i = 0; i < NUM_LANES; i = i + 1) begin : lane_rx
        lvds_lane_rx #(
            .SERDES_RATIO (SERDES_RATIO)
        ) u_lane (
            .data_p   (LVDS_DATA_P[i]),
            .data_n   (LVDS_DATA_N[i]),
            .clk      (lvds_clk_buf),
            .clk_div  (lvds_div_clk),
            .rst_n    (rst_n),
            .bitslip  (bitslip[i]),
            .data_out (raw_lane[i])
        );
    end
endgenerate

// ============================================================================
// 3. Bit Sync — finds training pattern, issues bitslip until locked
// ============================================================================

generate
    for (i = 0; i < NUM_LANES; i = i + 1) begin : bsync
        bit_sync #(
            .SERDES_RATIO   (SERDES_RATIO),
            .SYNC_PATTERN   (SYNC_PATTERN)
        ) u_bsync (
            .clk_div        (lvds_div_clk),
            .rst_n          (rst_n),
            .raw_data       (raw_lane[i]),
            .clk_active     (lvds_clk_active),
            .bitslip_out    (bitslip[i]),
            .aligned_word   (aligned_word[i]),
            .word_valid     (word_valid[i]),
            .sync_locked    ()   // per-lane; top-level uses combined below
        );
    end
endgenerate

// Combined lock indicator — all lanes locked
assign bit_sync_locked = &{word_valid[0], word_valid[1],
                            word_valid[2], word_valid[3]};

// ============================================================================
// 4. Word Sync — detects 0xF800 header, confirms frame alignment
// ============================================================================

word_sync #(
    .NUM_LANES    (NUM_LANES),
    .SYNC_PATTERN (SYNC_PATTERN)
) u_word_sync (
    .clk_div      (lvds_div_clk),
    .rst_n        (rst_n),
    .lane_words   ({aligned_word[3], aligned_word[2],
                    aligned_word[1], aligned_word[0]}),
    .words_in_valid (bit_sync_locked),
    .lane0_out    (lane0_word),
    .lane1_out    (lane1_word),
    .lane2_out    (lane2_word),
    .lane3_out    (lane3_word),
    .out_valid    (words_valid),
    .sync_locked  (word_sync_locked)
);

// ============================================================================
// 5. Frame Controller — SOF/EOF from clock-active detection + sample counter
// ============================================================================

frame_ctrl #(
    .SAMPLES_PER_CHIRP (SAMPLES_PER_CHIRP),
    .NUM_LANES         (NUM_LANES)
) u_frame_ctrl (
    .clk_div        (lvds_div_clk),
    .rst_n          (rst_n),
    .clk_active     (lvds_clk_active),
    .words_valid    (words_valid),
    .frame_start    (frame_start_int),
    .frame_end      (frame_end_int)
);

// ============================================================================
// 6. IQ De-interleaver
//    AWR1843 lane mapping (4 RX, 4-lane, complex interleaved):
//    Lane N → RX N, words alternate: I₀, Q₀, I₁, Q₁, ...
//    out_valid fires every 2 words (once per complete I+Q pair)
// ============================================================================

iq_deinterleave u_deint (
    .clk_div     (lvds_div_clk),
    .rst_n       (rst_n),
    .lane0       (lane0_word),
    .lane1       (lane1_word),
    .lane2       (lane2_word),
    .lane3       (lane3_word),
    .in_valid    (words_valid),
    .rx0_i       (rx0_i),  .rx0_q (rx0_q),
    .rx1_i       (rx1_i),  .rx1_q (rx1_q),
    .rx2_i       (rx2_i),  .rx2_q (rx2_q),
    .rx3_i       (rx3_i),  .rx3_q (rx3_q),
    .out_valid   (iq_valid_int)
);

// ============================================================================
// 7. Async FIFO — cross from lvds_div_clk to user_clk
//    Data bundle: 128 bits = {rx3_q, rx3_i, rx2_q, rx2_i,
//                              rx1_q, rx1_i, rx0_q, rx0_i}
// ============================================================================

async_iq_fifo #(
    .DATA_WIDTH (128),
    .DEPTH      (512),
    .ADDR_WIDTH (9)
) u_fifo (
    // Write side (LVDS clock domain)
    .wr_clk      (lvds_div_clk),
    .wr_rst_n    (rst_n),
    .wr_data     ({rx3_q, rx3_i, rx2_q, rx2_i,
                   rx1_q, rx1_i, rx0_q, rx0_i}),  // 128-bit bundle
    .wr_en       (iq_valid_int),
    .frame_start_wr (frame_start_int),
    .frame_end_wr   (frame_end_int),

    // Read side (user clock domain)
    .rd_clk      (user_clk),
    .rd_rst_n    (rst_n),
    .rd_data     ({rx3_q_data, rx3_i_data, rx2_q_data, rx2_i_data,
                   rx1_q_data, rx1_i_data, rx0_q_data, rx0_i_data}),
    .rd_en       (1'b1),
    .rd_valid    (iq_valid),
    .frame_start_rd (frame_start),
    .frame_end_rd   (frame_end)
);

endmodule
