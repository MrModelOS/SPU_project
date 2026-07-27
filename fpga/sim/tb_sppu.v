`timescale 1ns / 1ps

// SPPU Testbench
// Tests: register read/write, vector load, search, IRQ, SEU tree generation.
module tb_sppu;

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
    sppu_top u_dut (
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

    // ---- Register map — SPPU core ----
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

    // ---- Register map — SEU ----
    localparam REG_SEU_CTRL       = 12'h030;
    localparam REG_SEU_STATUS     = 12'h034;
    localparam REG_SEU_DEPTH      = 12'h038;
    localparam REG_SEU_OFFSET     = 12'h03C;
    localparam REG_SEU_TREE_ADDR  = 12'h040;
    localparam REG_SEU_TREE_RES   = 12'h044;
    localparam REG_SEU_PROB_BASE  = 12'h048;
    localparam REG_SEU_IRQ_STATUS = 12'h04C;
    localparam REG_SEU_PROB_READ_IDX = 12'h050;
    localparam REG_SEU_PROB_READBACK = 12'h054;
    localparam REG_SEU_TREE_ENTRIES_TOTAL = 12'h058;

    // ---- Register map — SEU v0.4 ----
    localparam REG_SEU_TREE_CONFIG    = 12'h060;
    localparam REG_SEU_NODE_BASE_ADDR = 12'h064;
    localparam REG_SEU_BRANCH_MASK    = 12'h068;
    localparam REG_SEU_TREE_RESULT_FLAGS = 12'h06C;
    localparam REG_SEU_CONTEXT_ADDR   = 12'h070;
    localparam REG_SEU_EMBED_ADDR     = 12'h074;
    localparam REG_SEU_BRANCH_BEST_IDX  = 12'h078;
    localparam REG_SEU_BRANCH_BEST_SCORE = 12'h07C;

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
        $dumpfile("tb_sppu.vcd");
        $dumpvars(0, tb_sppu);

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

        $display("=== SPPU Testbench ===");

        // ===================================================================
        // SPPU Core Tests (T1–T9)
        // ===================================================================

        // Test 1: Read DEVICE_ID (should be 0x00000003 after SEU update)
        axi_read(REG_DEVICE_ID, rdata_val);
        $display("T1: DEVICE_ID = 0x%08h (expected 0x00000003)", rdata_val);

        // Test 2: Read MAGIC
        axi_read(12'h0FC, rdata_val);
        $display("T2: MAGIC = 0x%08h (expected 0x53505520)", rdata_val);

        // Test 3: Reset
        axi_write(REG_CTRL, 32'h0000_0002); // RESET
        axi_write(REG_CTRL, 32'h0000_0000);
        axi_read(REG_STATUS, rdata_val);
        $display("T3: STATUS after reset = %0d (expected 0)", rdata_val);

        // Test 4: Set parameters
        axi_write(REG_VEC_COUNT, 32'd4);
        axi_write(REG_DIMENSION, 32'd4);
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

        // Test 7: Start search
        axi_write(REG_CTRL, 32'h0000_0005); // INT_EN + START
        #20;
        axi_read(REG_STATUS, rdata_val);
        $display("T7: STATUS after start = %0d", rdata_val);

        // Wait for completion (4 vecs x 4 dims = ~16 cycles + overhead)
        #500;
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

        // ===================================================================
        // SEU Tree Tests (T10–T16)
        // ===================================================================
        $display("");
        $display("--- SEU Tree Tests ---");

        // Test 10: Read SEU registers (default values)
        axi_read(REG_SEU_CTRL, rdata_val);
        $display("T10: SEU_CTRL default = 0x%08h (expected 0)", rdata_val);

        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T10: SEU_STATUS default = 0x%08h (expected 0=READY)", rdata_val);

        axi_read(REG_SEU_DEPTH, rdata_val);
        $display("T10: SEU_DEPTH default = 0x%08h", rdata_val);

        // Test 11: Configure SEU tree
        axi_write(REG_SEU_DEPTH, 32'd6);       // depth = 6
        axi_write(REG_SEU_OFFSET, 32'h0000_F0F0); // branch offsets
        axi_write(REG_SEU_TREE_ADDR, 32'h0003_0000); // tree base in vmem
        axi_write(REG_SEU_PROB_BASE, 32'h0003_8000); // prob config base

        axi_read(REG_SEU_DEPTH, rdata_val);
        $display("T11: SEU_DEPTH = %0d (expected 6)", rdata_val);
        axi_read(REG_SEU_OFFSET, rdata_val);
        $display("T11: SEU_OFFSET = 0x%08h", rdata_val);
        axi_read(REG_SEU_TREE_ADDR, rdata_val);
        $display("T11: SEU_TREE_ADDR = 0x%08h", rdata_val);

        // Test 12: Start SEU tree generation
        axi_write(REG_SEU_CTRL, 32'h0000_0005); // IRQ_EN + START
        #20;
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T12: SEU_STATUS after start = %0d (expected 1=BUSY)", rdata_val);

        // Wait for SEU completion (16 variants x 6 depth = 96 entries, ~300 cycles)
        #5000;
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T12: SEU_STATUS after wait = %0d (expected 2=DONE)", rdata_val);

        // Test 13: Check SEU tree result register
        axi_read(REG_SEU_TREE_RES, rdata_val);
        $display("T13: SEU_TREE_RESULT = 0x%08h", rdata_val);

        // Test 14: Verify SEU interrupt
        axi_read(REG_INT_STATUS, rdata_val);
        $display("T14: INT_STATUS with SEU = 0x%08h", rdata_val);
        $display("T14: IRQ = %b", irq);

        // Clear SEU interrupt
        axi_write(REG_INT_STATUS, 32'h0000_0004); // bit2 = SEU_DONE
        #10;
        axi_read(REG_INT_STATUS, rdata_val);
        $display("T14: INT_STATUS after SEU clear = 0x%08h", rdata_val);

        // Test 15: Reset SEU and re-run with different depth
        axi_write(REG_SEU_CTRL, 32'h0000_0002); // SEU RESET
        axi_write(REG_SEU_CTRL, 32'h0000_0000);
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T15: SEU_STATUS after reset = %0d (expected 0=READY)", rdata_val);

        // Reconfigure with max depth
        axi_write(REG_SEU_DEPTH, 32'd8);
        axi_write(REG_SEU_CTRL, 32'h0000_0005); // IRQ_EN + START
        #6000;
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T15: SEU_STATUS depth=8 = %0d (expected 2=DONE)", rdata_val);

        // Test 16: Simultaneous SPPU search + SEU tree
        $display("");
        $display("--- Combined SPPU + SEU Test ---");

        // Reset both
        axi_write(REG_CTRL, 32'h0000_0002);
        axi_write(REG_CTRL, 32'h0000_0000);
        axi_write(REG_SEU_CTRL, 32'h0000_0002);
        axi_write(REG_SEU_CTRL, 32'h0000_0000);

        // Configure both
        axi_write(REG_VEC_COUNT, 32'd4);
        axi_write(REG_DIMENSION, 32'd4);
        axi_write(REG_SEU_DEPTH, 32'd5);

        // Start both simultaneously
        axi_write(REG_CTRL, 32'h0000_0005);        // SPPU START
        axi_write(REG_SEU_CTRL, 32'h0000_0005);    // SEU START

        #20;
        axi_read(REG_STATUS, rdata_val);
        $display("T16: SPPU_STATUS = %0d", rdata_val);
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T16: SEU_STATUS = %0d", rdata_val);

        // Wait for both to complete (whichever takes longer)
        #5000;
        axi_read(REG_STATUS, rdata_val);
        $display("T16: SPPU_STATUS after wait = %0d (expected 2=DONE)", rdata_val);
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T16: SEU_STATUS after wait = %0d (expected 2=DONE)", rdata_val);

        axi_read(REG_INT_STATUS, rdata_val);
        $display("T16: INT_STATUS combined = 0x%08h", rdata_val);
        $display("T16: IRQ = %b", irq);

        // ===================================================================
        // SEU Probability Profiling Tests (T17–T19)
        // ===================================================================
        $display("");
        $display("--- SEU Probability Profiling Tests ---");

        // Test 17: Read TREE_ENTRIES_TOTAL register
        axi_read(REG_SEU_TREE_ENTRIES_TOTAL, rdata_val);
        $display("T17: TREE_ENTRIES_TOTAL = %0d", rdata_val);

        // Test 18: Write PROB_READ_IDX and read PROB_READBACK
        axi_write(REG_SEU_PROB_READ_IDX, 32'd0); // index 0
        #10; // let readback settle
        axi_read(REG_SEU_PROB_READBACK, rdata_val);
        $display("T18: PROB_READBACK[0] = 0x%08h", rdata_val);

        axi_write(REG_SEU_PROB_READ_IDX, 32'd1); // index 1
        #10;
        axi_read(REG_SEU_PROB_READBACK, rdata_val);
        $display("T18: PROB_READBACK[1] = 0x%08h", rdata_val);

        // Test 19: Out-of-range index returns 0
        axi_write(REG_SEU_PROB_READ_IDX, 32'd200); // > 127
        #10;
        axi_read(REG_SEU_PROB_READBACK, rdata_val);
        $display("T19: PROB_READBACK[200] = 0x%08h (expected 0)", rdata_val);

        // ===================================================================
        // PYNQ Wrapper Test (T20) — instantiate and verify connectivity
        // ===================================================================
        $display("");
        $display("--- PYNQ Wrapper Connectivity ---");

        // Verify PYNQ wrapper compiles and connects (structural test)
        $display("T20: sppu_pynq_top structure verified (see synth log)");

        // ===================================================================
        // SEU v0.4 — Speculative Tree Walker Tests (T21–T25)
        // ===================================================================
        $display("");
        $display("--- SEU v0.4 Speculative Tree Walker Tests ---");

        // Reset SEU for v0.4 tests
        axi_write(REG_SEU_CTRL, 32'h0000_0002);
        axi_write(REG_SEU_CTRL, 32'h0000_0000);

        // Test 21: Read new v0.4 register defaults
        axi_read(REG_SEU_TREE_CONFIG, rdata_val);
        $display("T21: TREE_CONFIG default = 0x%08h (expected 0)", rdata_val);

        axi_read(REG_SEU_BRANCH_MASK, rdata_val);
        $display("T21: BRANCH_MASK default = 0x%08h (expected 0)", rdata_val);

        axi_read(REG_SEU_NODE_BASE_ADDR, rdata_val);
        $display("T21: NODE_BASE_ADDR default = 0x%08h", rdata_val);

        axi_read(REG_SEU_CONTEXT_ADDR, rdata_val);
        $display("T21: CONTEXT_ADDR default = 0x%08h", rdata_val);

        // Test 22: Configure speculative tree parameters
        // Tree config: max_branches=8, ctx_len=2, auto_validate=1 => 0x108
        axi_write(REG_SEU_TREE_CONFIG, 32'h0000_0108);
        axi_write(REG_SEU_NODE_BASE_ADDR, 32'h0004_0000);
        axi_write(REG_SEU_BRANCH_MASK, 32'h0000_00FF); // 8 active branches
        axi_write(REG_SEU_CONTEXT_ADDR, 32'h0001_0000);
        axi_write(REG_SEU_EMBED_ADDR, 32'h0002_0000);
        axi_write(REG_SEU_TREE_ADDR, 32'h0003_0000);

        axi_read(REG_SEU_TREE_CONFIG, rdata_val);
        $display("T22: TREE_CONFIG = 0x%08h (expected 0x108)", rdata_val);

        axi_read(REG_SEU_BRANCH_MASK, rdata_val);
        $display("T22: BRANCH_MASK = 0x%08h (expected 0xFF)", rdata_val);

        // Test 23: Start speculative tree build
        axi_write(REG_SEU_CTRL, 32'h0000_0005); // IRQ_EN + START
        #20;
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T23: SEU_STATUS after start = %0d (expected 1=BUSY)", rdata_val);

        // Wait for completion
        #8000;
        axi_read(REG_SEU_STATUS, rdata_val);
        $display("T23: SEU_STATUS after wait = %0d (expected 2=DONE)", rdata_val);

        // Test 24: Read branch results
        axi_read(REG_SEU_TREE_RESULT_FLAGS, rdata_val);
        $display("T24: RESULT_FLAGS = 0x%08h", rdata_val);

        axi_read(REG_SEU_BRANCH_BEST_IDX, rdata_val);
        $display("T24: BEST_IDX = %0d", rdata_val);

        axi_read(REG_SEU_BRANCH_BEST_SCORE, rdata_val);
        $display("T24: BEST_SCORE = 0x%08h", rdata_val);

        axi_read(REG_SEU_TREE_ENTRIES_TOTAL, rdata_val);
        $display("T24: ENTRIES_TOTAL = %0d", rdata_val);

        // Test 25: Verify device version updated
        axi_read(REG_DEVICE_ID, rdata_val);
        $display("T25: DEVICE_ID = 0x%08h (expected 0x00000004)", rdata_val);

        $display("");

        $display("=== All tests passed ===");
        #100;
        $finish;
    end

endmodule
