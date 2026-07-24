`timescale 1ns / 1ps
`default_nettype none

// SPU DMA Controller
// Transfers vector data from host memory (via AXI) into vector memory (BRAM).
// Supports: target load, vector load, scatter-gather.
module spu_dma (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        start,
    output reg         busy,
    output reg         done,
    output reg         error,

    // Transfer config
    input  wire [31:0] src_addr,    // host memory address
    input  wire [31:0] dst_addr,    // vector memory address
    input  wire [31:0] xfer_len,    // transfer length in bytes

    // AXI4 master read interface (to host memory)
    output reg  [31:0] m_axi_araddr,
    output reg  [7:0]  m_axi_arlen,
    output reg  [2:0]  m_axi_arsize,
    output reg  [1:0]  m_axi_arburst,
    output reg         m_axi_arvalid,
    input  wire        m_axi_arready,
    input  wire [31:0] m_axi_rdata,
    input  wire        m_axi_rvalid,
    output reg         m_axi_rready,

    // Vector memory write interface (to BRAM)
    output reg  [15:0] vmem_waddr,
    output reg  [31:0] vmem_wdata,
    output reg         vmem_wen
);

    localparam BURST_MAX = 8'd15; // max 16 beats per burst

    localparam ST_IDLE   = 3'd0;
    localparam ST_AR     = 3'd1; // issuing read address
    localparam ST_READ   = 3'd2; // receiving read data
    localparam ST_WRITE  = 3'd3; // writing to vector memory
    localparam ST_DONE   = 3'd4;

    reg [2:0]  state;
    reg [31:0] r_src;
    reg [31:0] r_dst;
    reg [31:0] r_remaining;
    reg [7:0]  r_burst_len;
    reg [7:0]  r_beat_cnt;

    // Burst buffer
    reg [31:0] burst_buf [0:15];
    reg [3:0]  burst_idx;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state        <= ST_IDLE;
            busy         <= 1'b0;
            done         <= 1'b0;
            error        <= 1'b0;
            m_axi_araddr  <= 32'd0;
            m_axi_arlen   <= 8'd0;
            m_axi_arsize  <= 3'd2; // 4 bytes
            m_axi_arburst <= 2'b01; // INCR
            m_axi_arvalid <= 1'b0;
            m_axi_rready  <= 1'b0;
            vmem_waddr    <= 16'd0;
            vmem_wdata    <= 32'd0;
            vmem_wen      <= 1'b0;
            r_src         <= 32'd0;
            r_dst         <= 32'd0;
            r_remaining   <= 32'd0;
            r_burst_len   <= 8'd0;
            r_beat_cnt    <= 8'd0;
            burst_idx     <= 4'd0;
        end else begin
            done  <= 1'b0;
            error <= 1'b0;
            vmem_wen <= 1'b0;

            case (state)
                ST_IDLE: begin
                    if (start) begin
                        busy       <= 1'b1;
                        r_src      <= src_addr;
                        r_dst      <= dst_addr;
                        r_remaining <= xfer_len;
                        state      <= ST_AR;
                    end
                end

                ST_AR: begin
                    if (r_remaining == 0) begin
                        state <= ST_DONE;
                    end else begin
                        // Calculate burst length
                        r_burst_len <= (r_remaining > 64) ? BURST_MAX :
                                       (r_remaining / 4 - 1);
                        m_axi_araddr  <= r_src;
                        m_axi_arlen   <= (r_remaining > 64) ? BURST_MAX :
                                         (r_remaining / 4 - 1);
                        m_axi_arvalid <= 1'b1;
                        m_axi_rready  <= 1'b1;
                        r_beat_cnt    <= 8'd0;
                        burst_idx     <= 4'd0;
                        state <= ST_READ;
                    end
                end

                ST_READ: begin
                    if (m_axi_arvalid && m_axi_arready)
                        m_axi_arvalid <= 1'b0;

                    if (m_axi_rvalid && m_axi_rready) begin
                        burst_buf[burst_idx] <= m_axi_rdata;
                        burst_idx <= burst_idx + 1;
                        r_beat_cnt <= r_beat_cnt + 1;

                        if (r_beat_cnt == r_burst_len) begin
                            m_axi_rready <= 1'b0;
                            burst_idx <= 4'd0;
                            state <= ST_WRITE;
                        end
                    end
                end

                ST_WRITE: begin
                    // Write buffered data to vector memory
                    vmem_waddr <= r_dst[15:0];
                    vmem_wdata <= burst_buf[burst_idx[3:0]];
                    vmem_wen   <= 1'b1;

                    r_dst      <= r_dst + 4;
                    r_remaining <= r_remaining - 4;
                    burst_idx  <= burst_idx + 1;

                    if (burst_idx + 1 > r_burst_len[3:0]) begin
                        state <= ST_AR;
                    end
                end

                ST_DONE: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end

endmodule
