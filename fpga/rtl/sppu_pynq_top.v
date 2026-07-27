`timescale 1ns / 1ps
`default_nettype none

module sppu_pynq #(
    parameter C_AXI_DATA_WIDTH = 32,
    parameter C_AXI_ADDR_WIDTH = 12
)(
    input  wire       sys_clk,
    input  wire       sys_rst_n,

    output wire [4:0] led,

    output wire       uart_tx,
    input  wire       uart_rx,

    output wire       irq,

    // Zynq PS AXI-Lite Master (GP0) — connected via PS-to-PL
    input  wire [C_AXI_ADDR_WIDTH-1:0]    s_axi_awaddr,
    input  wire                            s_axi_awvalid,
    output wire                            s_axi_awready,
    input  wire [C_AXI_DATA_WIDTH-1:0]    s_axi_wdata,
    input  wire                            s_axi_wvalid,
    output wire                            s_axi_wready,
    output wire [1:0]                     s_axi_bresp,
    output wire                            s_axi_bvalid,
    input  wire                            s_axi_bready,
    input  wire [C_AXI_ADDR_WIDTH-1:0]    s_axi_araddr,
    input  wire                            s_axi_arvalid,
    output wire                            s_axi_arready,
    output wire [C_AXI_DATA_WIDTH-1:0]    s_axi_rdata,
    output wire [1:0]                     s_axi_rresp,
    output wire                            s_axi_rvalid,
    input  wire                            s_axi_rready,

    // Zynq PS AXI4 Master (HP0) — for DMA reads from DDR
    output wire [31:0]  m_axi_araddr,
    output wire [7:0]   m_axi_arlen,
    output wire [2:0]   m_axi_arsize,
    output wire [1:0]   m_axi_arburst,
    output wire         m_axi_arvalid,
    input  wire         m_axi_arready,
    input  wire [31:0]  m_axi_rdata,
    input  wire         m_axi_rvalid,
    output wire         m_axi_rready
);

    wire clk = sys_clk;
    wire rst_n = sys_rst_n;

    sppu_top #(
        .C_AXI_DATA_WIDTH(C_AXI_DATA_WIDTH),
        .C_AXI_ADDR_WIDTH(C_AXI_ADDR_WIDTH)
    ) u_sppu (
        .clk            (clk),
        .rst_n          (rst_n),
        .irq            (irq),
        .s_axi_awaddr   (s_axi_awaddr),
        .s_axi_awvalid  (s_axi_awvalid),
        .s_axi_awready  (s_axi_awready),
        .s_axi_wdata    (s_axi_wdata),
        .s_axi_wvalid   (s_axi_wvalid),
        .s_axi_wready   (s_axi_wready),
        .s_axi_bresp    (s_axi_bresp),
        .s_axi_bvalid   (s_axi_bvalid),
        .s_axi_bready   (s_axi_bready),
        .s_axi_araddr   (s_axi_araddr),
        .s_axi_arvalid  (s_axi_arvalid),
        .s_axi_arready  (s_axi_arready),
        .s_axi_rdata    (s_axi_rdata),
        .s_axi_rresp    (s_axi_rresp),
        .s_axi_rvalid   (s_axi_rvalid),
        .s_axi_rready   (s_axi_rready),
        .m_axi_araddr   (m_axi_araddr),
        .m_axi_arlen    (m_axi_arlen),
        .m_axi_arsize   (m_axi_arsize),
        .m_axi_arburst  (m_axi_arburst),
        .m_axi_arvalid  (m_axi_arvalid),
        .m_axi_arready  (m_axi_arready),
        .m_axi_rdata    (m_axi_rdata),
        .m_axi_rvalid   (m_axi_rvalid),
        .m_axi_rready   (m_axi_rready)
    );

    reg [31:0] heartbeat_cnt;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            heartbeat_cnt <= 32'd0;
        else
            heartbeat_cnt <= heartbeat_cnt + 32'd1;
    end

    assign led[0] = heartbeat_cnt[24];
    assign led[1] = irq;
    assign led[2] = 1'b0;
    assign led[3] = 1'b0;
    assign led[4] = 1'b0;

    assign uart_tx = 1'b1;

endmodule