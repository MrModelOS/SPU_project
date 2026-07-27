`timescale 1ns / 1ps
`default_nettype none

// SPPU PYNQ/Zynq Top-Level Wrapper
// Connects sppu_top to Zynq PS via AXI GP port.
// For PYNQ-Z2 / PYNQ-Z1 / Zybo-Z7 boards with XC7Z020.
module sppu_pynq_top #(
    parameter C_AXI_DATA_WIDTH = 32,
    parameter C_AXI_ADDR_WIDTH = 12
)(
    // System
    input  wire       sys_clk,     // 125 MHz from PS or oscillator
    input  wire       sys_rst_n,   // Active-low reset (from PS or button)

    // LEDs
    output wire [4:0] led,

    // UART (debug / register access from host)
    output wire       uart_tx,
    input  wire       uart_rx,

    // Interrupt to PS
    output wire       irq
);

    // ---- Clock gen (PS CLK → fabric) ----
    // On PYNQ boards, sys_clk comes from FCLK_CLK0 (100 MHz)
    // or an external oscillator. For standalone use, instantiate
    // a clock wizard if needed. Here we assume sys_clk is
    // already the correct fabric clock.

    wire clk = sys_clk;
    wire rst_n = sys_rst_n;

    // ---- AXI-Lite master from PS (register access) ----
    // In a real Zynq design, these come from the PS AXI GP port
    // via an AXI Interconnect. For standalone testing, we expose
    // them as top-level ports that can be driven by a UART-to-AXI
    // bridge or directly from PS fabric.

    wire [C_AXI_ADDR_WIDTH-1:0] s_axi_awaddr;
    wire                        s_axi_awvalid;
    wire                        s_axi_awready;
    wire [C_AXI_DATA_WIDTH-1:0] s_axi_wdata;
    wire                        s_axi_wvalid;
    wire                        s_axi_wready;
    wire [1:0]                  s_axi_bresp;
    wire                        s_axi_bvalid;
    wire                        s_axi_bready;
    wire [C_AXI_ADDR_WIDTH-1:0] s_axi_araddr;
    wire                        s_axi_arvalid;
    wire                        s_axi_arready;
    wire [C_AXI_DATA_WIDTH-1:0] s_axi_rdata;
    wire [1:0]                  s_axi_rresp;
    wire                        s_axi_rvalid;
    wire                        s_axi_rready;

    // AXI4 master (DMA — for Zynq, this goes to HP port)
    wire [31:0] m_axi_araddr;
    wire [7:0]  m_axi_arlen;
    wire [2:0]  m_axi_arsize;
    wire [1:0]  m_axi_arburst;
    wire        m_axi_arvalid;
    wire        m_axi_arready;
    wire [31:0] m_axi_rdata;
    wire        m_axi_rvalid;
    wire        m_axi_rready;

    // ---- SPPU Core ----
    sppu_top #(
        .C_AXI_DATA_WIDTH(C_AXI_DATA_WIDTH),
        .C_AXI_ADDR_WIDTH(C_AXI_ADDR_WIDTH)
    ) u_sppu (
        .clk            (clk),
        .rst_n          (rst_n),
        .irq            (irq),
        // AXI-Lite slave
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
        // AXI4 master (DMA)
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

    // ---- LED status ----
    // LED[0] = heartbeat (blinks)
    // LED[1] = SPPU busy
    // LED[2] = SPPU done
    // LED[3] = SEU busy
    // LED[4] = IRQ active

    reg [31:0] heartbeat_cnt;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            heartbeat_cnt <= 32'd0;
        else
            heartbeat_cnt <= heartbeat_cnt + 32'd1;
    end

    assign led[0] = heartbeat_cnt[24];  // ~1 Hz blink at 100 MHz
    assign led[1] = (s_axi_rdata[1:0] == 2'd1); // BUSY
    assign led[2] = (s_axi_rdata[1:0] == 2'd2); // DONE
    assign led[3] = irq;                          // IRQ
    assign led[4] = sys_rst_n;                    // reset status

    // ---- UART-to-AXI-Lite bridge (stub) ----
    // In production, replace with uart_axi_bridge module
    // that receives register read/write commands over UART
    // and converts them to AXI-Lite transactions.
    // For now, expose AXI signals for direct PS connection.

    // Tie off AXI-Lite master when not driven by PS
    assign s_axi_awvalid = 1'b0;
    assign s_axi_wvalid  = 1'b0;
    assign s_axi_bready  = 1'b1;
    assign s_axi_arvalid = 1'b0;
    assign s_axi_rready  = 1'b1;

    // Tie off DMA master (active only when PS drives it)
    assign m_axi_arready = 1'b0;

endmodule
