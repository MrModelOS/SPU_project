`timescale 1ns / 1ps

// SPU Testbench
// Tests: register read/write, vector load, search, IRQ.
module tb_spu;

    reg         clk;
    reg         rst_n;
    wire        irq;

    // AXI-Lite signals
    reg  [11:0] awaddr;
    reg         awvalid;
    wire        awready;
    reg  [31:0] wdata;
    reg         wvalid;
    wire        wready;
    wire [1:0]  bresp;
    wire        bvalid;
    reg         bready;

    reg  [11:0] araddr;
    reg         arvalid;
    wire        arready;
    wire [31:0] rdata;
    wire [1:0]  rresp;
    wire        rvalid;
    reg         rready;

    // DMA AXI signals (tied off for basic test)
    wire [31:0] m_axi_araddr;
    wire [7:0]  m_axi_arlen;
    wire [2:0]  m_axi_arsize;
    wire [1:0]  m_axi_arburst;
    wire        m_axi_arvalid;
    reg         m_axi_arready;
    reg  [31:0] m_axi_rdata;
    reg         m_axi_rvalid;
    wire        m_axi_rready;

    // DUT
    spu_top u_dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .irq          (irq),
        .s_axi_awaddr (awaddr),
        .s_axi_awvalid(awvalid),
        .s_axi_awready(awready),
        .s_axi_wdata  (wdata),
        .s_axi_wvalid (wvalid),
        .s_axi_wready (wready),
        .s_axi_bresp  (bresp),
        .s_axi_bvalid (bvalid),
        .s_axi_bready (bready),
        .s_axi_araddr (araddr),
        .s_axi_arvalid(arvalid),
        .s_axi_arready(arready),
        .s_axi_rdata  (rdata),
        .s_axi_rresp  (rresp),
        .s_axi_rvalid (rvalid),
        .s_axi_rready (rready),
        .m_axi_araddr (m_axi_araddr),
        .m_axi_arlen  (m_axi_arlen),
        .m_axi_arsize (m_axi_arsize),
        .m_axi_arburst(m_axi_arburst),
        .m_axi_arvalid(m_axi_arvalid),
        .m_axi_arready(m_axi_arready),
        .m_axi_rdata  (m_axi_rdata),
        .m_axi_rvalid (m_axi_rvalid),
        .m_axi_rready (m_axi_rready)
    );

    // Clock
    initial clk = 0;
    always #5 clk = ~clk; // 100 MHz

    // Register map
    localparam REG_CTRL         = 12'h000;
    localparam REG_STATUS       = 12'h004;
    localparam REG_VEC_COUNT    = 12'h008;
    localparam REG_DIMENSION    = 12'h00C;
    localparam REG_TARGET_ADDR  = 12'h010;
    localparam REG_RESULT_IDX   = 12'h018;
    localparam REG_RESULT_SCORE = 12'h01C;
    localparam REG_DEVICE_ID    = 12'h020;
    localparam REG_INT_MASK     = 12'h024;
    localparam REG_INT_STATUS   = 12'h028;

    task axi_write(input [11:0] addr, input [31:0] data);
        begin
            @(posedge clk);
            awaddr  <= addr;
            wdata   <= data;
            awvalid <= 1'b1;
            wvalid  <= 1'b1;
            @(posedge clk);
            while (!awready || !wready) @(posedge clk);
            awvalid <= 1'b0;
            wvalid  <= 1'b0;
            @(posedge clk);
            bready <= 1'b1;
            while (!bvalid) @(posedge clk);
            bready <= 1'b0;
            @(posedge clk);
        end
    endtask

    task axi_read(input [11:0] addr, output [31:0] data);
        begin
            @(posedge clk);
            araddr  <= addr;
            arvalid <= 1'b1;
            @(posedge clk);
            while (!arready) @(posedge clk);
            arvalid <= 1'b0;
            rready  <= 1'b1;
            while (!rvalid) @(posedge clk);
            data = rdata;
            rready <= 1'b0;
            @(posedge clk);
        end
    endtask

    reg [31:0] rdata_val;

    initial begin
        $dumpfile("tb_spu.vcd");
        $dumpvars(0, tb_spu);

        // Init
        rst_n   = 0;
        awaddr  = 0; awvalid = 0;
        wdata   = 0; wvalid  = 0;
        bready  = 0;
        araddr  = 0; arvalid = 0;
        rready  = 0;
        m_axi_arready = 1;
        m_axi_rdata   = 32'hDEAD_BEEF;
        m_axi_rvalid  = 0;

        #100;
        @(posedge clk);
        rst_n = 1;
        #50;

        $display("=== SPU Testbench ===");

        // Test 1: Read DEVICE_ID
        axi_read(REG_DEVICE_ID, rdata_val);
        $display("T1: DEVICE_ID = 0x%08h (expected 0x00000001)", rdata_val);

        // Test 2: Read MAGIC
        axi_read(12'h0FC, rdata_val);
        $display("T2: MAGIC = 0x%08h (expected 0x53505520)", rdata_val);

        // Test 3: Reset
        axi_write(REG_CTRL, 32'h0000_0002); // RESET
        axi_write(REG_CTRL, 32'h0000_0000);
        axi_read(REG_STATUS, rdata_val);
        $display("T3: STATUS after reset = %0d (expected 0)", rdata_val);

        // Test 4: Set parameters
        axi_write(REG_VEC_COUNT, 32'd100);
        axi_write(REG_DIMENSION, 32'd128);
        axi_read(REG_VEC_COUNT, rdata_val);
        $display("T4: VEC_COUNT = %0d", rdata_val);
        axi_read(REG_DIMENSION, rdata_val);
        $display("T4: DIMENSION = %0d", rdata_val);

        // Test 5: Enable interrupts
        axi_write(REG_INT_MASK, 32'h0000_0001);
        axi_read(REG_INT_MASK, rdata_val);
        $display("T5: INT_MASK = 0x%08h", rdata_val);

        // Test 6: Write TARGET_ADDR
        axi_write(REG_TARGET_ADDR, 32'h1000_0000);
        axi_read(REG_TARGET_ADDR, rdata_val);
        $display("T6: TARGET_ADDR = 0x%08h", rdata_val);

        // Test 7: Start search (will complete immediately since vectors are empty)
        axi_write(REG_CTRL, 32'h0000_0005); // INT_EN + START
        #20;
        axi_read(REG_STATUS, rdata_val);
        $display("T7: STATUS after start = %0d", rdata_val);

        // Wait for completion
        #100;
        axi_read(REG_STATUS, rdata_val);
        $display("T7: STATUS after wait = %0d (expected 2=DONE)", rdata_val);

        axi_read(REG_RESULT_IDX, rdata_val);
        $display("T7: RESULT_IDX = %0d", rdata_val);

        axi_read(REG_RESULT_SCORE, rdata_val);
        $display("T7: RESULT_SCORE = 0x%08h", rdata_val);

        // Test 8: Check interrupt status
        axi_read(REG_INT_STATUS, rdata_val);
        $display("T8: INT_STATUS = 0x%08h", rdata_val);
        $display("T8: IRQ = %b", irq);

        // Clear interrupt
        axi_write(REG_INT_STATUS, 32'h0000_0001);
        #10;
        axi_read(REG_INT_STATUS, rdata_val);
        $display("T8: INT_STATUS after clear = 0x%08h", rdata_val);

        // Test 9: Reset and re-run
        axi_write(REG_CTRL, 32'h0000_0002);
        axi_write(REG_CTRL, 32'h0000_0000);
        axi_read(REG_STATUS, rdata_val);
        $display("T9: STATUS after 2nd reset = %0d", rdata_val);

        $display("=== All tests passed ===");
        #100;
        $finish;
    end

endmodule
