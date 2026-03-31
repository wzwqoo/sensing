//==============================================================================
// async_iq_fifo.v
// Asynchronous FIFO: crosses IQ data from lvds_div_clk → user_clk.
//
// Data width: 128 bits = {rx3_q, rx3_i, rx2_q, rx2_i,
//                          rx1_q, rx1_i, rx0_q, rx0_i}  (8 × 16-bit)
// Depth     : 512 entries (holds 2 full chirps of 256 samples, 4 RX)
//
// Also crosses frame_start and frame_end flags via toggle-synchronizer.
//
// FIFO implementation: Gray-coded read/write pointers synchronized across
// domains — standard 2-FF synchronizer on each pointer.
//
// For production use, consider replacing with Xilinx xpm_fifo_async IP
// for better timing closure guarantees.
//==============================================================================

`timescale 1ns/1ps

module async_iq_fifo #(
    parameter DATA_WIDTH = 128,  // 4 RX × 2 (I+Q) × 16 bits
    parameter DEPTH      = 512,  // must be power of 2
    parameter ADDR_WIDTH = 9     // log2(DEPTH)
)(
    // Write port (LVDS clock domain)
    input  wire                  wr_clk,
    input  wire                  wr_rst_n,
    input  wire [DATA_WIDTH-1:0] wr_data,
    input  wire                  wr_en,
    input  wire                  frame_start_wr,   // pulse in wr_clk domain
    input  wire                  frame_end_wr,

    // Read port (user clock domain)
    input  wire                  rd_clk,
    input  wire                  rd_rst_n,
    output reg  [DATA_WIDTH-1:0] rd_data,
    input  wire                  rd_en,
    output wire                  rd_valid,
    output wire                  frame_start_rd,   // pulse in rd_clk domain
    output wire                  frame_end_rd,

    // Status
    output wire                  full,
    output wire                  empty
);

// ============================================================================
// Memory
// ============================================================================

reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];

// ============================================================================
// Write logic
// ============================================================================

reg [ADDR_WIDTH:0] wr_ptr_bin;   // binary write pointer (extra bit for full detect)
reg [ADDR_WIDTH:0] wr_ptr_gray;  // Gray-coded
wire [ADDR_WIDTH:0] wr_ptr_bin_next = wr_ptr_bin + 1;

always @(posedge wr_clk or negedge wr_rst_n) begin
    if (!wr_rst_n) begin
        wr_ptr_bin  <= 0;
        wr_ptr_gray <= 0;
    end else if (wr_en && !full) begin
        mem[wr_ptr_bin[ADDR_WIDTH-1:0]] <= wr_data;
        wr_ptr_bin  <= wr_ptr_bin_next;
        wr_ptr_gray <= (wr_ptr_bin_next >> 1) ^ wr_ptr_bin_next;
    end
end

// ============================================================================
// Read logic
// ============================================================================

reg [ADDR_WIDTH:0] rd_ptr_bin;
reg [ADDR_WIDTH:0] rd_ptr_gray;
wire [ADDR_WIDTH:0] rd_ptr_bin_next = rd_ptr_bin + 1;

always @(posedge rd_clk or negedge rd_rst_n) begin
    if (!rd_rst_n) begin
        rd_ptr_bin  <= 0;
        rd_ptr_gray <= 0;
        rd_data     <= 0;
    end else if (rd_en && !empty) begin
        rd_data     <= mem[rd_ptr_bin[ADDR_WIDTH-1:0]];
        rd_ptr_bin  <= rd_ptr_bin_next;
        rd_ptr_gray <= (rd_ptr_bin_next >> 1) ^ rd_ptr_bin_next;
    end
end

assign rd_valid = rd_en && !empty;

// ============================================================================
// Gray pointer synchronizers
// ============================================================================

// Sync wr_ptr_gray into rd_clk domain
reg [ADDR_WIDTH:0] wr_gray_sync1, wr_gray_sync2;
always @(posedge rd_clk or negedge rd_rst_n) begin
    if (!rd_rst_n) begin
        wr_gray_sync1 <= 0;
        wr_gray_sync2 <= 0;
    end else begin
        wr_gray_sync1 <= wr_ptr_gray;
        wr_gray_sync2 <= wr_gray_sync1;
    end
end

// Sync rd_ptr_gray into wr_clk domain
reg [ADDR_WIDTH:0] rd_gray_sync1, rd_gray_sync2;
always @(posedge wr_clk or negedge wr_rst_n) begin
    if (!wr_rst_n) begin
        rd_gray_sync1 <= 0;
        rd_gray_sync2 <= 0;
    end else begin
        rd_gray_sync1 <= rd_ptr_gray;
        rd_gray_sync2 <= rd_gray_sync1;
    end
end

// ============================================================================
// Full / empty logic
//   Full  (wr domain): wr_ptr_gray == {~rd_gray_sync2[MSB:MSB-1], rd_gray_sync2[MSB-2:0]}
//   Empty (rd domain): rd_ptr_gray == wr_gray_sync2
// ============================================================================

assign full  = (wr_ptr_gray == {~rd_gray_sync2[ADDR_WIDTH:ADDR_WIDTH-1],
                                  rd_gray_sync2[ADDR_WIDTH-2:0]});
assign empty = (rd_ptr_gray == wr_gray_sync2);

// ============================================================================
// Frame flag crossing: toggle synchronizer
// A pulse in wr domain sets a toggle FF; a 2-FF synchronizer in rd domain
// detects the edge and re-generates a 1-cycle pulse.
// ============================================================================

// frame_start
reg fs_toggle_wr;
always @(posedge wr_clk or negedge wr_rst_n) begin
    if (!wr_rst_n) fs_toggle_wr <= 1'b0;
    else if (frame_start_wr) fs_toggle_wr <= ~fs_toggle_wr;
end

reg [2:0] fs_sync_rd;
always @(posedge rd_clk or negedge rd_rst_n) begin
    if (!rd_rst_n) fs_sync_rd <= 3'b000;
    else           fs_sync_rd <= {fs_sync_rd[1:0], fs_toggle_wr};
end
assign frame_start_rd = fs_sync_rd[2] ^ fs_sync_rd[1];

// frame_end
reg fe_toggle_wr;
always @(posedge wr_clk or negedge wr_rst_n) begin
    if (!wr_rst_n) fe_toggle_wr <= 1'b0;
    else if (frame_end_wr) fe_toggle_wr <= ~fe_toggle_wr;
end

reg [2:0] fe_sync_rd;
always @(posedge rd_clk or negedge rd_rst_n) begin
    if (!rd_rst_n) fe_sync_rd <= 3'b000;
    else           fe_sync_rd <= {fe_sync_rd[1:0], fe_toggle_wr};
end
assign frame_end_rd = fe_sync_rd[2] ^ fe_sync_rd[1];

endmodule
