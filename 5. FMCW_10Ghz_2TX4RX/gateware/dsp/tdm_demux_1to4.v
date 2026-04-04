// =============================================================================
//  tdm_demux_1to4.v
//  Routes a TDM stream (1 sample/clock) back to 4 individual slow channels.
//
//  The critical problem: the FIR Compiler is a deep pipeline.  A sample that
//  enters on clock N does not appear at the output until clock N+LATENCY.
//  If we just look at the current cnt to decide where to route, we route to
//  the WRONG channel because cnt has advanced LATENCY ticks since that sample
//  entered.
//
//  Solution: shift-register tag tracker.
//  The MUX outputs tdm_ch_id alongside tdm_data.  We pass tdm_ch_id into the
//  FIR via a separate LATENCY-deep shift register.  The tail of that SR always
//  tells us "which channel produced the sample currently at the FIR output".
//
//  FIR_LATENCY value
//  -----------------
//  Read from Vivado FIR Compiler synthesis report:
//    "Latency = N cycles" in the IP summary tab.
//  Typical values for 101-tap, fully-pipelined:
//    Hilbert FIR  (101 taps, Hilbert type):  ~58 clocks
//    Bandpass FIR (101 taps, Single Rate):   ~58 clocks
//  Add 1 for the MUX output register: FIR_LATENCY = reported_latency + 1.
//  If results are off by one channel, increment or decrement by 1.
//
//  Vivado FIR Compiler port mapping (m_axis side):
//    .m_axis_data_tdata  (tdm_fir_data)
//    .m_axis_data_tvalid (tdm_fir_valid)
//  Pass tdm_ch_id from the MUX directly into ch_tag_in.
//
//  Parameters
//  ----------
//  DATA_W      : data bus width (must match FIR Compiler output width)
//  FIR_LATENCY : pipeline depth of the upstream FIR IP (in fast clock cycles)
// =============================================================================

`default_nettype none

module tdm_demux_1to4 #(
    parameter DATA_W      = 16,
    parameter FIR_LATENCY = 58    // ← update from your Vivado FIR report
)(
    input  wire             clk,
    input  wire             rst_n,

    // ── TDM input from FIR m_axis port ────────────────────────────────────
    input  wire [DATA_W-1:0] tdm_fir_data,
    input  wire              tdm_fir_valid,

    // ── Channel tag from the upstream MUX (tdm_ch_id port) ───────────────
    // This is the tag of the sample that is currently ENTERING the FIR.
    // We delay it by FIR_LATENCY to align with when it EXITS the FIR.
    input  wire [1:0]        ch_tag_in,

    // ── 4 demuxed output channels ──────────────────────────────────────────
    output reg  [DATA_W-1:0] ch0_data,
    output reg  [DATA_W-1:0] ch1_data,
    output reg  [DATA_W-1:0] ch2_data,
    output reg  [DATA_W-1:0] ch3_data,
    output reg               ch0_valid,
    output reg               ch1_valid,
    output reg               ch2_valid,
    output reg               ch3_valid
);

    // ── Tag shift register: depth = FIR_LATENCY ───────────────────────────
    // Synthesises as SRL16/SRL32 (LUT-based shift register) in Xilinx tools.
    // Each bit of the 2-bit tag shifts independently.
    // For depths > 32, inferred as distributed RAM or register chain.
    reg [1:0] tag_sr [0:FIR_LATENCY-1];

    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < FIR_LATENCY; i = i + 1)
                tag_sr[i] <= 2'd0;
        end else begin
            tag_sr[0] <= ch_tag_in;
            for (i = 1; i < FIR_LATENCY; i = i + 1)
                tag_sr[i] <= tag_sr[i-1];
        end
    end

    // ── Delayed tag: which channel is at the FIR output RIGHT NOW ─────────
    wire [1:0] ch_tag_out = tag_sr[FIR_LATENCY-1];

    // ── Route FIR output to correct channel register ───────────────────────
    // All channel valids are cleared each cycle; only the current target fires.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ch0_data <= {DATA_W{1'b0}}; ch0_valid <= 1'b0;
            ch1_data <= {DATA_W{1'b0}}; ch1_valid <= 1'b0;
            ch2_data <= {DATA_W{1'b0}}; ch2_valid <= 1'b0;
            ch3_data <= {DATA_W{1'b0}}; ch3_valid <= 1'b0;
        end else begin
            // Default: clear all valids
            ch0_valid <= 1'b0;
            ch1_valid <= 1'b0;
            ch2_valid <= 1'b0;
            ch3_valid <= 1'b0;

            if (tdm_fir_valid) begin // after 58 cycles, FIR output is valid and aligned with ch_tag_out
                case (ch_tag_out)
                    2'd0: begin ch0_data <= tdm_fir_data; ch0_valid <= 1'b1; end
                    2'd1: begin ch1_data <= tdm_fir_data; ch1_valid <= 1'b1; end
                    2'd2: begin ch2_data <= tdm_fir_data; ch2_valid <= 1'b1; end
                    2'd3: begin ch3_data <= tdm_fir_data; ch3_valid <= 1'b1; end
                    default: ; // should never happen
                endcase
            end
        end
    end

endmodule

`default_nettype wire
