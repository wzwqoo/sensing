// =============================================================================
//  rd_map_collector.v
//
//  Collects Doppler FFT output and writes the complete Range-Doppler map to
//  DDR3 as 128-beat sequential bursts (one burst per range bin).
//
//  Storage layout (row-major, natural order):
//    sample[k][n] at RD_MAP_BASE + (k * N_CHIRPS + n) * SAMPLE_BYTES
//
//  After all N_RANGE_BINS bursts written, cfar_start is pulsed to kick CFAR.
//  Waits for cfar_done before asserting frame_complete.
//
//  ── AXI4 master write interface ───────────────────────────────────────────
//    AWSIZE, AWBURST, WSTRB, BREADY tied constant.
//    m_axi_wlast is a combinatorial wire (not registered) — see Fix 1.
//
//  ── Bugs fixed vs previous version ───────────────────────────────────────
//    1. m_axi_wlast was registered one beat early, causing a phantom 129th beat
//       to be presented to MIG with wlast=1 after the burst ended.
//       Fixed by making wlast a combinatorial output wire driven by beat_cnt
//       and wvalid — it is high on exactly the same cycle as the last data beat.
//    2. wr_addr_r not initialised at reset — first burst went to address X.
//       Fixed by adding reset clause to the address always block.
// =============================================================================

`default_nettype none

module rd_map_collector #(
    parameter DATA_W       = 32,
    parameter ADDR_W       = 32,
    parameter N_RANGE_BINS = 1024,
    parameter N_CHIRPS     = 128,
    parameter SAMPLE_BYTES = 4,
    parameter RD_MAP_BASE  = 32'h0020_0000
)(
    input  wire clk,
    input  wire rst_n,

    // ── AXI-Stream input — Doppler FFT m_axis_data ────────────────────────
    input  wire [DATA_W-1:0] s_axis_tdata,
    input  wire              s_axis_tvalid,
    input  wire              s_axis_tlast,
    output reg               s_axis_tready,

    // ── AXI4 master write address channel (AW) ────────────────────────────
    output reg  [ADDR_W-1:0] m_axi_awaddr,
    output reg  [7:0]        m_axi_awlen,
    output wire [2:0]        m_axi_awsize,
    output wire [1:0]        m_axi_awburst,
    output reg               m_axi_awvalid,
    input  wire              m_axi_awready,

    // ── AXI4 master write data channel (W) ────────────────────────────────
    output reg  [DATA_W-1:0]   m_axi_wdata,
    output wire [DATA_W/8-1:0] m_axi_wstrb,
    // FIX 1: wlast is a wire — combinatorial, not registered
    // This ensures wlast arrives on the same cycle as the last data beat.
    output wire                m_axi_wlast,
    output reg                 m_axi_wvalid,
    input  wire                m_axi_wready,

    // ── AXI4 master write response channel (B) ────────────────────────────
    input  wire [1:0]        m_axi_bresp,
    input  wire              m_axi_bvalid,
    output wire              m_axi_bready,

    // ── CFAR HLS IP handshake ─────────────────────────────────────────────
    output reg               cfar_start,
    input  wire              cfar_done,

    // ── Status ────────────────────────────────────────────────────────────
    output reg               busy,
    output reg               frame_complete
);

    // ── Tied constant AXI4 signals ────────────────────────────────────────
    assign m_axi_awsize  = 3'd2;
    assign m_axi_awburst = 2'b01;
    assign m_axi_wstrb   = {(DATA_W/8){1'b1}};
    assign m_axi_bready  = 1'b1;

    // ── State encoding ────────────────────────────────────────────────────
    localparam S_IDLE      = 3'd0;
    localparam S_ISSUE_AW  = 3'd1;
    localparam S_WAIT_AW   = 3'd2;
    localparam S_WRITE     = 3'd3;
    localparam S_WAIT_B    = 3'd4;
    localparam S_NEXT_BIN  = 3'd5;
    localparam S_KICK_CFAR = 3'd6;
    localparam S_WAIT_CFAR = 3'd7;

    localparam K_W  = $clog2(N_RANGE_BINS);
    localparam BT_W = $clog2(N_CHIRPS);

    localparam [K_W-1:0]  K_MAX      = N_RANGE_BINS - 1;
    localparam [BT_W-1:0] BEAT_LAST  = N_CHIRPS - 1;
    localparam [ADDR_W-1:0] BURST_STRIDE = N_CHIRPS * SAMPLE_BYTES;

    reg [2:0]    state;
    reg [K_W-1:0]  k;
    reg [BT_W-1:0] beat_cnt;

    // FIX 1: wlast driven combinatorially — high when wvalid is high and
    // beat_cnt is on the last beat.  No registration, no timing ambiguity.
    assign m_axi_wlast = m_axi_wvalid && (beat_cnt == BEAT_LAST);

    // FIX 2: wr_addr_r initialised at reset
    reg [ADDR_W-1:0] wr_addr_r;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            wr_addr_r <= RD_MAP_BASE;  // FIX 2: known value, not X
        else
            wr_addr_r <= RD_MAP_BASE
                         + ( {{(ADDR_W-K_W){1'b0}}, k} * BURST_STRIDE );
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state          <= S_IDLE;
            k              <= {K_W{1'b0}};
            beat_cnt       <= {BT_W{1'b0}};
            busy           <= 1'b0;
            cfar_start     <= 1'b0;
            frame_complete <= 1'b0;
            s_axis_tready  <= 1'b0;
            m_axi_awvalid  <= 1'b0;
            m_axi_awaddr   <= {ADDR_W{1'b0}};
            m_axi_awlen    <= 8'd0;
            m_axi_wvalid   <= 1'b0;
            m_axi_wdata    <= {DATA_W{1'b0}};
        end else begin
            cfar_start     <= 1'b0;
            frame_complete <= 1'b0;

            case (state)

                // ── S_IDLE ────────────────────────────────────────────────
                S_IDLE: begin
                    k             <= {K_W{1'b0}};
                    beat_cnt      <= {BT_W{1'b0}};
                    busy          <= 1'b0;
                    s_axis_tready <= 1'b0;
                    m_axi_awvalid <= 1'b0;
                    m_axi_wvalid  <= 1'b0;
                    if (s_axis_tvalid) begin
                        state <= S_ISSUE_AW;
                        busy  <= 1'b1;
                    end
                end

                // ── S_ISSUE_AW ────────────────────────────────────────────
                // wr_addr_r settled during S_IDLE or S_NEXT_BIN.
                S_ISSUE_AW: begin
                    m_axi_awaddr  <= wr_addr_r;
                    m_axi_awlen   <= N_CHIRPS - 1;
                    m_axi_awvalid <= 1'b1;
                    beat_cnt      <= {BT_W{1'b0}};
                    state         <= S_WAIT_AW;
                end

                // ── S_WAIT_AW ─────────────────────────────────────────────
                S_WAIT_AW: begin
                    if (m_axi_awvalid && m_axi_awready) begin
                        m_axi_awvalid <= 1'b0;
                        s_axis_tready <= 1'b1;
                        state         <= S_WRITE;
                    end
                end

                // ── S_WRITE ───────────────────────────────────────────────
                // Transfer one FFT sample per beat.
                // FIX 1: m_axi_wlast is a combinatorial wire driven by beat_cnt.
                // No pre-assertion needed — wlast goes high automatically when
                // beat_cnt == BEAT_LAST on the same cycle wvalid is high.
                S_WRITE: begin
                    if (s_axis_tvalid && s_axis_tready
                        && (!m_axi_wvalid || m_axi_wready)) begin

                        m_axi_wdata  <= s_axis_tdata;
                        m_axi_wvalid <= 1'b1;
                        beat_cnt     <= beat_cnt + 1'b1;
                        // wlast is combinatorial: no assignment needed here

                        if (beat_cnt == BEAT_LAST) begin
                            // Last beat — stop accepting FFT data, wait for B
                            s_axis_tready <= 1'b0;
                            state         <= S_WAIT_B;
                        end
                    end else if (m_axi_wvalid && m_axi_wready) begin
                        // W accepted but no new data yet — clear valid
                        m_axi_wvalid <= 1'b0;
                    end
                end

                // ── S_WAIT_B ──────────────────────────────────────────────
                // Drain any final wvalid, then wait for B response.
                S_WAIT_B: begin
                    if (m_axi_wvalid && m_axi_wready)
                        m_axi_wvalid <= 1'b0;
                    if (m_axi_bvalid)   // bready tied 1
                        state <= S_NEXT_BIN;
                end

                // ── S_NEXT_BIN ────────────────────────────────────────────
                S_NEXT_BIN: begin
                    if (k < K_MAX) begin
                        k     <= k + 1'b1;
                        state <= S_ISSUE_AW;
                    end else begin
                        k     <= {K_W{1'b0}};
                        state <= S_KICK_CFAR;
                    end
                end

                // ── S_KICK_CFAR ───────────────────────────────────────────
                S_KICK_CFAR: begin
                    cfar_start <= 1'b1;
                    state      <= S_WAIT_CFAR;
                end

                // ── S_WAIT_CFAR ───────────────────────────────────────────
                S_WAIT_CFAR: begin
                    if (cfar_done) begin
                        frame_complete <= 1'b1;
                        busy           <= 1'b0;
                        state          <= S_IDLE;
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire