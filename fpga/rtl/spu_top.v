`timescale 1ns / 1ps
`default_nettype none

// SPU Top-Level Module
// Connects: AXI-Lite registers, DMA controller, vector memory, dot-product engine.
// Synthesizable for Xilinx/Intel FPGA with PCIe hard block.
module spu_top #(
    parameter C_AXI_DATA_WIDTH = 32,
    parameter C_AXI_ADDR_WIDTH = 12
)(
    input  wire        clk,
    input  wire        rst_n,

    // Interrupt to host
    output wire        irq,

    // AXI-Lite slave (register access from PCIe)
    input  wire [C_AXI_ADDR_WIDTH-1:0] s_axi_awaddr,
    input  wire                        s_axi_awvalid,
    output wire                        s_axi_awready,
    input  wire [C_AXI_DATA_WIDTH-1:0] s_axi_wdata,
    input  wire                        s_axi_wvalid,
    output wire                        s_axi_wready,
    output wire [1:0]                  s_axi_bresp,
    output wire                        s_axi_bvalid,
    input  wire                        s_axi_bready,
    input  wire [C_AXI_ADDR_WIDTH-1:0] s_axi_araddr,
    input  wire                        s_axi_arvalid,
    output wire                        s_axi_arready,
    output wire [C_AXI_DATA_WIDTH-1:0] s_axi_rdata,
    output wire [1:0]                  s_axi_rresp,
    output wire                        s_axi_rvalid,
    input  wire                        s_axi_rready,

    // AXI4 master (DMA reads from host memory)
    output wire [31:0] m_axi_araddr,
    output wire [7:0]  m_axi_arlen,
    output wire [2:0]  m_axi_arsize,
    output wire [1:0]  m_axi_arburst,
    output wire        m_axi_arvalid,
    input  wire        m_axi_arready,
    input  wire [31:0] m_axi_rdata,
    input  wire        m_axi_rvalid,
    output wire        m_axi_rready
);

    // ---- Internal wires ----
    wire [31:0] ctrl;
    wire [31:0] status;
    wire [31:0] vec_count;
    wire [31:0] dimension;
    wire [31:0] target_addr;
    wire [31:0] result_idx;
    wire [31:0] result_score;
    wire [31:0] int_mask;
    wire [31:0] int_status;

    wire        dp_start;
    wire        dp_busy;
    wire        dp_done;
    wire [31:0] dp_result_idx;
    wire [31:0] dp_result_score;

    wire        dma_start;
    wire        dma_busy;
    wire        dma_done;
    wire        dma_error;
    wire [31:0] dma_src;
    wire [31:0] dma_dst;
    wire [31:0] dma_len;

    wire [17:0] vmem_a_addr;
    wire [31:0] vmem_a_wdata;
    wire        vmem_a_wen;
    wire [17:0] vmem_b_addr;
    wire [31:0] vmem_b_rdata;

    wire [11:0] dp_target_raddr;
    wire [31:0] dp_target_rdata;

    // ---- Control logic ----
    reg [1:0] status_reg;
    reg       start_prev;

    wire ctrl_start_pulse;
    assign ctrl_start_pulse = ctrl[0] & ~start_prev;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            status_reg <= 2'd0;
            start_prev <= 1'b0;
        end else begin
            start_prev <= ctrl[0];

            if (ctrl[1]) begin // RESET
                status_reg <= 2'd0;
            end else if (ctrl_start_pulse && !dp_busy && !dma_busy) begin
                status_reg <= 2'd1; // BUSY — start DMA + compute
            end else if (dp_done) begin
                status_reg <= 2'd2; // DONE
            end
        end
    end

    // DMA trigger on START
    assign dma_start = ctrl_start_pulse & (status_reg == 2'd0);
    assign dma_src   = target_addr;
    assign dma_dst   = 32'd0; // target goes to addr 0 in vmem
    assign dma_len   = dimension * 4; // bytes

    // Dot-product trigger after DMA completes (or immediately for pre-loaded vectors)
    assign dp_start  = ctrl_start_pulse;

    // Interrupt generation
    reg irq_pending;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            irq_pending <= 1'b0;
        else if (dp_done && int_mask[0])
            irq_pending <= 1'b1;
        else if (int_status[0]) // clear on write-1-to-clear
            irq_pending <= 1'b0;
    end
    assign irq = irq_pending;
    assign int_status = {30'd0, irq_pending, 1'b0};

    assign status = {30'd0, status_reg};

    // ---- Register File ----
    spu_regs u_regs (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axi_awaddr (s_axi_awaddr),
        .s_axi_awvalid(s_axi_awvalid),
        .s_axi_awready(s_axi_awready),
        .s_axi_wdata  (s_axi_wdata),
        .s_axi_wvalid (s_axi_wvalid),
        .s_axi_wready (s_axi_wready),
        .s_axi_bresp  (s_axi_bresp),
        .s_axi_bvalid (s_axi_bvalid),
        .s_axi_bready (s_axi_bready),
        .s_axi_araddr (s_axi_araddr),
        .s_axi_arvalid(s_axi_arvalid),
        .s_axi_arready(s_axi_arready),
        .s_axi_rdata  (s_axi_rdata),
        .s_axi_rresp  (s_axi_rresp),
        .s_axi_rvalid (s_axi_rvalid),
        .s_axi_rready (s_axi_rready),
        .ctrl         (ctrl),
        .status       (status),
        .vec_count    (vec_count),
        .dimension    (dimension),
        .target_addr  (target_addr),
        .result_idx   (dp_result_idx),
        .result_score (dp_result_score),
        .int_mask     (int_mask),
        .int_status_io(int_status)
    );

    // ---- DMA Controller ----
    spu_dma u_dma (
        .clk          (clk),
        .rst_n        (rst_n),
        .start        (dma_start),
        .busy         (dma_busy),
        .done         (dma_done),
        .error        (dma_error),
        .src_addr     (dma_src),
        .dst_addr     (dma_dst),
        .xfer_len     (dma_len),
        .m_axi_araddr (m_axi_araddr),
        .m_axi_arlen  (m_axi_arlen),
        .m_axi_arsize (m_axi_arsize),
        .m_axi_arburst(m_axi_arburst),
        .m_axi_arvalid(m_axi_arvalid),
        .m_axi_arready(m_axi_arready),
        .m_axi_rdata  (m_axi_rdata),
        .m_axi_rvalid (m_axi_rvalid),
        .m_axi_rready (m_axi_rready),
        .vmem_waddr   (vmem_a_addr[15:0]),
        .vmem_wdata   (vmem_a_wdata),
        .vmem_wen     (vmem_a_wen)
    );

    // ---- Vector Memory ----
    spu_vecmem u_vmem (
        .clk    (clk),
        .a_addr (vmem_a_addr),
        .a_wdata(vmem_a_wdata),
        .a_wen  (vmem_a_wen),
        .b_addr (vmem_b_addr),
        .b_rdata(vmem_b_rdata)
    );

    // ---- Dot-Product Engine ----
    spu_dotprod u_dp (
        .clk          (clk),
        .rst_n        (rst_n),
        .start        (dp_start),
        .busy         (dp_busy),
        .done         (dp_done),
        .vec_count    (vec_count),
        .dimension    (dimension),
        .target_raddr (dp_target_raddr),
        .target_rdata (dp_target_rdata),
        .vec_raddr    (vmem_b_addr[11:0]),
        .vec_rdata    (vmem_b_rdata),
        .result_idx   (dp_result_idx),
        .result_score (dp_result_score)
    );

endmodule
