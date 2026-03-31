//==============================================================================
// tb_awr1843_lvds_top.v
// Simulation testbench for the AWR1843 LVDS capture design.
//
// Stimulus model — correct AWR1843 4-RX interleaved I/Q format:
//   - Generates a source-synchronous LVDS clock (75 MHz bit clock)
//   - 4 data lanes, one per RX receiver
//   - Each lane carries: [0xF800 header][I₀][Q₀][I₁][Q₁]...[I_{N-1}][Q_{N-1}]
//   - Clock goes idle (static high) between chirps
//   - Monitors: frame_start / frame_end pulses, IQ output all 4 RX
//==============================================================================

`timescale 1ns/1ps

module tb_awr1843_lvds_top;

// ============================================================================
// Parameters (match DUT)
// ============================================================================

localparam NUM_LANES         = 4;
localparam SAMPLES_PER_CHIRP = 8;   // small for fast simulation
localparam SERDES_RATIO      = 8;
localparam SYNC_PATTERN      = 16'hF800;

// Bit clock: 75 MHz → 13.333 ns; half-period = 6.667 ns
localparam real BIT_CLK_HALF = 6.667;

// ============================================================================
// DUT ports
// ============================================================================

reg        LVDS_CLK_P, LVDS_CLK_N;
reg  [NUM_LANES-1:0] LVDS_DATA_P, LVDS_DATA_N;
reg        user_clk, rst_n;

wire [15:0] rx0_i_data, rx0_q_data;
wire [15:0] rx1_i_data, rx1_q_data;
wire [15:0] rx2_i_data, rx2_q_data;
wire [15:0] rx3_i_data, rx3_q_data;
wire        iq_valid, frame_start, frame_end;
wire        bit_sync_locked, word_sync_locked, lvds_clk_active;

// ============================================================================
// DUT instantiation
// ============================================================================

awr1843_lvds_top #(
    .NUM_LANES         (NUM_LANES),
    .SAMPLES_PER_CHIRP (SAMPLES_PER_CHIRP),
    .SERDES_RATIO      (SERDES_RATIO),
    .SYNC_PATTERN      (SYNC_PATTERN)
) dut (
    .LVDS_CLK_P        (LVDS_CLK_P),
    .LVDS_CLK_N        (LVDS_CLK_N),
    .LVDS_DATA_P       (LVDS_DATA_P),
    .LVDS_DATA_N       (LVDS_DATA_N),
    .user_clk          (user_clk),
    .rst_n             (rst_n),
    .rx0_i_data        (rx0_i_data),  .rx0_q_data (rx0_q_data),
    .rx1_i_data        (rx1_i_data),  .rx1_q_data (rx1_q_data),
    .rx2_i_data        (rx2_i_data),  .rx2_q_data (rx2_q_data),
    .rx3_i_data        (rx3_i_data),  .rx3_q_data (rx3_q_data),
    .iq_valid          (iq_valid),
    .frame_start       (frame_start),
    .frame_end         (frame_end),
    .bit_sync_locked   (bit_sync_locked),
    .word_sync_locked  (word_sync_locked),
    .lvds_clk_active   (lvds_clk_active)
);

// ============================================================================
// Clocks
// ============================================================================

initial user_clk = 0;
always #5 user_clk = ~user_clk;   // 100 MHz

initial begin
    LVDS_CLK_P = 1'b1;
    LVDS_CLK_N = 1'b0;
end

// ============================================================================
// send_word task
// Serializes one 16-bit word onto all 4 lanes simultaneously in DDR format.
// lane_data[lane] holds the word to send on that lane.
// AWR1843 sends MSB first. DDR: posedge=even bit, negedge=odd bit.
// ============================================================================

reg [15:0] lane_data [0:NUM_LANES-1];

task send_word;
    integer b, ln;
    begin
        // 16 bits, 2 per DDR cycle = 8 DDR half-cycles
        for (b = 15; b >= 0; b = b - 2) begin
            // Posedge phase: bit b
            for (ln = 0; ln < NUM_LANES; ln = ln + 1) begin
                LVDS_DATA_P[ln] =  lane_data[ln][b];
                LVDS_DATA_N[ln] = ~lane_data[ln][b];
            end
            #BIT_CLK_HALF;
            LVDS_CLK_P = ~LVDS_CLK_P;
            LVDS_CLK_N = ~LVDS_CLK_N;

            // Negedge phase: bit b-1
            for (ln = 0; ln < NUM_LANES; ln = ln + 1) begin
                LVDS_DATA_P[ln] =  lane_data[ln][b-1];
                LVDS_DATA_N[ln] = ~lane_data[ln][b-1];
            end
            #BIT_CLK_HALF;
            LVDS_CLK_P = ~LVDS_CLK_P;
            LVDS_CLK_N = ~LVDS_CLK_N;
        end
    end
endtask

// ============================================================================
// send_chirp task
// Sends one complete chirp burst:
//   1. Header word (0xF800) on all lanes
//   2. SAMPLES_PER_CHIRP complex samples per lane
//      Each sample = 2 consecutive words: I word, then Q word
//
// Lane encoding:
//   Lane 0 → RX0: I=0xA0xx, Q=0xA1xx
//   Lane 1 → RX1: I=0xB0xx, Q=0xB1xx
//   Lane 2 → RX2: I=0xC0xx, Q=0xC1xx
//   Lane 3 → RX3: I=0xD0xx, Q=0xD1xx
// where xx = sample index (lower byte)
// ============================================================================

task send_chirp;
    integer s;
    begin
        $display("[TB %0t] ─── Chirp START ───", $time);

        // ── Header word on all lanes ──
        lane_data[0] = SYNC_PATTERN;
        lane_data[1] = SYNC_PATTERN;
        lane_data[2] = SYNC_PATTERN;
        lane_data[3] = SYNC_PATTERN;
        send_word;

        // ── Complex samples: I then Q, interleaved per lane ──
        for (s = 0; s < SAMPLES_PER_CHIRP; s = s + 1) begin
            // I words for all 4 RX simultaneously
            lane_data[0] = 16'hA000 | (s & 8'hFF);  // RX0 I
            lane_data[1] = 16'hB000 | (s & 8'hFF);  // RX1 I
            lane_data[2] = 16'hC000 | (s & 8'hFF);  // RX2 I
            lane_data[3] = 16'hD000 | (s & 8'hFF);  // RX3 I
            send_word;

            // Q words for all 4 RX simultaneously
            lane_data[0] = 16'hA100 | (s & 8'hFF);  // RX0 Q
            lane_data[1] = 16'hB100 | (s & 8'hFF);  // RX1 Q
            lane_data[2] = 16'hC100 | (s & 8'hFF);  // RX2 Q
            lane_data[3] = 16'hD100 | (s & 8'hFF);  // RX3 Q
            send_word;
        end

        $display("[TB %0t] ─── Chirp END   ───", $time);
    end
endtask

// ============================================================================
// idle_gap task — hold clock static (between chirps)
// ============================================================================

task idle_gap;
    input integer ns;
    begin
        LVDS_CLK_P  = 1'b1;
        LVDS_CLK_N  = 1'b0;
        LVDS_DATA_P = 4'b0000;
        LVDS_DATA_N = 4'b1111;
        #ns;
    end
endtask

// ============================================================================
// Stimulus — reset, then 3 chirps with idle gaps
// ============================================================================

integer chirp_num;

initial begin
    $dumpfile("tb_awr1843_lvds_top.vcd");
    $dumpvars(0, tb_awr1843_lvds_top);

    rst_n       = 1'b0;
    LVDS_DATA_P = 4'b0;
    LVDS_DATA_N = 4'b1111;
    #200;
    rst_n = 1'b1;
    #100;

    for (chirp_num = 0; chirp_num < 3; chirp_num = chirp_num + 1) begin
        send_chirp;
        idle_gap(500);
    end

    #2000;
    $display("[TB] Simulation complete");
    $finish;
end

// ============================================================================
// Monitor — display SOF/EOF and every IQ sample from all 4 RX
// ============================================================================

always @(posedge user_clk) begin
    if (frame_start)
        $display("[TB %0t] *** FRAME_START ***", $time);
    if (frame_end)
        $display("[TB %0t] *** FRAME_END ***",   $time);
    if (iq_valid)
        $display("[TB %0t] IQ out | RX0: I=%04h Q=%04h | RX1: I=%04h Q=%04h | RX2: I=%04h Q=%04h | RX3: I=%04h Q=%04h",
                 $time,
                 rx0_i_data, rx0_q_data,
                 rx1_i_data, rx1_q_data,
                 rx2_i_data, rx2_q_data,
                 rx3_i_data, rx3_q_data);
end

endmodule
