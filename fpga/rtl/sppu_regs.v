`timescale 1ns / 1ps
`default_nettype none

// SPPU Register File
// BAR0 MMIO interface — matches sppu_device.h register map
module sppu_regs (
    input  wire        clk,
    input  wire        rst_n,

    // AXI-Lite slave interface (from PCIe)
    input  wire [11:0] s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output wire        s_axi_bvalid,
    input  wire        s_axi_bready,

    input  wire [11:0] s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    // Internal register interface — SPPU core
    output reg  [31:0] ctrl,
    input  wire [31:0] status,
    output reg  [31:0] vec_count,
    output reg  [31:0] dimension,
    output reg  [31:0] target_addr,
    input  wire [31:0] result_idx,
    input  wire [31:0] result_score,
    output reg  [31:0] int_mask,
    inout  wire [31:0] int_status_io,

    // Internal register interface — SEU (Speculative Execution Unit)
    output reg  [31:0] seu_ctrl,
    input  wire [31:0] seu_status,
    output reg  [31:0] seu_depth,
    output reg  [31:0] seu_offset,
    output reg  [31:0] seu_tree_addr,
    input  wire [31:0] seu_tree_result,
    output reg  [31:0] seu_prob_base,
    input  wire [31:0] seu_irq_status,

    // SEU probability profiling
    output reg  [31:0] seu_prob_read_idx,
    input  wire [31:0] seu_prob_readback,
    input  wire [31:0] seu_tree_entries_total,

    // SEU v0.4 — speculative tree walker
    output reg  [31:0] seu_tree_config,
    output reg  [31:0] seu_node_base_addr,
    output reg  [15:0] seu_branch_mask,
    input  wire [15:0] seu_tree_result_flags,
    output reg  [31:0] seu_context_addr,
    output reg  [31:0] seu_embed_addr,
    input  wire [31:0] seu_branch_best_idx,
    input  wire [31:0] seu_branch_best_score
);

    localparam SPPU_REG_CTRL         = 12'h000;
    localparam SPPU_REG_STATUS       = 12'h004;
    localparam SPPU_REG_VEC_COUNT    = 12'h008;
    localparam SPPU_REG_DIMENSION    = 12'h00C;
    localparam SPPU_REG_TARGET_ADDR  = 12'h010;
    localparam SPPU_REG_RESULT_IDX   = 12'h018;
    localparam SPPU_REG_RESULT_SCORE = 12'h01C;
    localparam SPPU_REG_DEVICE_ID    = 12'h020;
    localparam SPPU_REG_INT_MASK     = 12'h024;
    localparam SPPU_REG_INT_STATUS   = 12'h028;

    // SEU registers
    localparam SPPU_REG_SEU_CTRL       = 12'h030;
    localparam SPPU_REG_SEU_STATUS     = 12'h034;
    localparam SPPU_REG_SEU_DEPTH      = 12'h038;
    localparam SPPU_REG_SEU_OFFSET     = 12'h03C;
    localparam SPPU_REG_SEU_TREE_ADDR  = 12'h040;
    localparam SPPU_REG_SEU_TREE_RES   = 12'h044;
    localparam SPPU_REG_SEU_PROB_BASE  = 12'h048;
    localparam SPPU_REG_SEU_IRQ_STATUS       = 12'h04C;
    localparam SPPU_REG_SEU_PROB_READ_IDX    = 12'h050;
    localparam SPPU_REG_SEU_PROB_READBACK    = 12'h054;
    localparam SPPU_REG_SEU_TREE_ENTRIES_TOTAL = 12'h058;

    // SEU v0.4 — speculative tree walker registers
    localparam SPPU_REG_SEU_TREE_CONFIG     = 12'h060;
    localparam SPPU_REG_SEU_NODE_BASE_ADDR  = 12'h064;
    localparam SPPU_REG_SEU_BRANCH_MASK     = 12'h068;
    localparam SPPU_REG_SEU_TREE_RESULT_FLAGS = 12'h06C;
    localparam SPPU_REG_SEU_CONTEXT_ADDR    = 12'h070;
    localparam SPPU_REG_SEU_EMBED_ADDR      = 12'h074;
    localparam SPPU_REG_SEU_BRANCH_BEST_IDX = 12'h078;
    localparam SPPU_REG_SEU_BRANCH_BEST_SCORE = 12'h07C;

    localparam SPPU_DEVICE_VERSION   = 32'h0000_1001;  /* v1.0 — Zynq-7010 */
    localparam SPPU_MAGIC            = 32'h5350_5520;

    reg [1:0]  aw_state, ar_state;
    reg        aw_done, ar_done;
    reg [31:0] ar_rdata;

    assign s_axi_awready = (aw_state == 2'd0);
    assign s_axi_wready  = (aw_state == 2'd0) || (aw_state == 2'd1);
    assign s_axi_bresp   = 2'b00;
    assign s_axi_bvalid  = aw_done;

    assign s_axi_arready = (ar_state == 2'd0);
    assign s_axi_rresp   = 2'b00;
    assign s_axi_rvalid  = ar_done;
    assign s_axi_rdata   = ar_rdata;

    // Write channel FSM
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            aw_state      <= 2'd0;
            aw_done       <= 1'b0;
            ctrl          <= 32'd0;
            vec_count     <= 32'd0;
            dimension     <= 32'd0;
            target_addr   <= 32'd0;
            int_mask      <= 32'd0;
            seu_ctrl      <= 32'd0;
            seu_depth     <= 32'd0;
            seu_offset    <= 32'd0;
            seu_tree_addr <= 32'd0;
            seu_prob_base <= 32'd0;
            seu_prob_read_idx <= 32'd0;
            seu_tree_config    <= 32'd0;
            seu_node_base_addr <= 32'd0;
            seu_branch_mask    <= 16'd0;
            seu_context_addr   <= 32'd0;
            seu_embed_addr     <= 32'd0;
        end else begin
            if (s_axi_bready)
                aw_done <= 1'b0;

            case (aw_state)
                2'd0: begin
                    if (s_axi_awvalid && s_axi_wvalid) begin
                        // Can accept both address and data simultaneously
                        aw_state <= 2'd2;
                        case (s_axi_awaddr)
                            SPPU_REG_CTRL:          ctrl          <= s_axi_wdata;
                            SPPU_REG_VEC_COUNT:     vec_count     <= s_axi_wdata;
                            SPPU_REG_DIMENSION:     dimension     <= s_axi_wdata;
                            SPPU_REG_TARGET_ADDR:   target_addr   <= s_axi_wdata;
                            SPPU_REG_INT_MASK:      int_mask      <= s_axi_wdata;
                            SPPU_REG_INT_STATUS:    ; // RW1C handled externally
                            SPPU_REG_SEU_CTRL:      seu_ctrl      <= s_axi_wdata;
                            SPPU_REG_SEU_DEPTH:     seu_depth     <= s_axi_wdata;
                            SPPU_REG_SEU_OFFSET:    seu_offset    <= s_axi_wdata;
                            SPPU_REG_SEU_TREE_ADDR: seu_tree_addr <= s_axi_wdata;
                            SPPU_REG_SEU_PROB_BASE: seu_prob_base <= s_axi_wdata;
                            SPPU_REG_SEU_IRQ_STATUS: ; // RW1C handled externally
                            SPPU_REG_SEU_PROB_READ_IDX: seu_prob_read_idx <= s_axi_wdata;
                            SPPU_REG_SEU_TREE_CONFIG:    seu_tree_config    <= s_axi_wdata;
                            SPPU_REG_SEU_NODE_BASE_ADDR: seu_node_base_addr <= s_axi_wdata;
                            SPPU_REG_SEU_BRANCH_MASK:    seu_branch_mask    <= s_axi_wdata[15:0];
                            SPPU_REG_SEU_CONTEXT_ADDR:   seu_context_addr   <= s_axi_wdata;
                            SPPU_REG_SEU_EMBED_ADDR:     seu_embed_addr     <= s_axi_wdata;
                            default: ;
                        endcase
                    end else if (s_axi_awvalid) begin
                        aw_state <= 2'd1;
                    end else if (s_axi_wvalid) begin
                        aw_state <= 2'd3;
                    end
                end
                2'd1: begin // waiting for W
                    if (s_axi_wvalid) begin
                        aw_state <= 2'd2;
                        case (s_axi_awaddr)
                            SPPU_REG_CTRL:          ctrl          <= s_axi_wdata;
                            SPPU_REG_VEC_COUNT:     vec_count     <= s_axi_wdata;
                            SPPU_REG_DIMENSION:     dimension     <= s_axi_wdata;
                            SPPU_REG_TARGET_ADDR:   target_addr   <= s_axi_wdata;
                            SPPU_REG_INT_MASK:      int_mask      <= s_axi_wdata;
                            SPPU_REG_SEU_CTRL:      seu_ctrl      <= s_axi_wdata;
                            SPPU_REG_SEU_DEPTH:     seu_depth     <= s_axi_wdata;
                            SPPU_REG_SEU_OFFSET:    seu_offset    <= s_axi_wdata;
                            SPPU_REG_SEU_TREE_ADDR: seu_tree_addr <= s_axi_wdata;
                            SPPU_REG_SEU_PROB_BASE: seu_prob_base <= s_axi_wdata;
                            SPPU_REG_SEU_PROB_READ_IDX: seu_prob_read_idx <= s_axi_wdata;
                            SPPU_REG_SEU_TREE_CONFIG:    seu_tree_config    <= s_axi_wdata;
                            SPPU_REG_SEU_NODE_BASE_ADDR: seu_node_base_addr <= s_axi_wdata;
                            SPPU_REG_SEU_BRANCH_MASK:    seu_branch_mask    <= s_axi_wdata[15:0];
                            SPPU_REG_SEU_CONTEXT_ADDR:   seu_context_addr   <= s_axi_wdata;
                            SPPU_REG_SEU_EMBED_ADDR:     seu_embed_addr     <= s_axi_wdata;
                            default: ;
                        endcase
                    end
                end
                2'd3: begin // waiting for AW
                    if (s_axi_awvalid) begin
                        aw_state <= 2'd2;
                        case (s_axi_awaddr)
                            SPPU_REG_CTRL:          ctrl          <= s_axi_wdata;
                            SPPU_REG_VEC_COUNT:     vec_count     <= s_axi_wdata;
                            SPPU_REG_DIMENSION:     dimension     <= s_axi_wdata;
                            SPPU_REG_TARGET_ADDR:   target_addr   <= s_axi_wdata;
                            SPPU_REG_INT_MASK:      int_mask      <= s_axi_wdata;
                            SPPU_REG_SEU_CTRL:      seu_ctrl      <= s_axi_wdata;
                            SPPU_REG_SEU_DEPTH:     seu_depth     <= s_axi_wdata;
                            SPPU_REG_SEU_OFFSET:    seu_offset    <= s_axi_wdata;
                            SPPU_REG_SEU_TREE_ADDR: seu_tree_addr <= s_axi_wdata;
                            SPPU_REG_SEU_PROB_BASE: seu_prob_base <= s_axi_wdata;
                            SPPU_REG_SEU_PROB_READ_IDX: seu_prob_read_idx <= s_axi_wdata;
                            SPPU_REG_SEU_TREE_CONFIG:    seu_tree_config    <= s_axi_wdata;
                            SPPU_REG_SEU_NODE_BASE_ADDR: seu_node_base_addr <= s_axi_wdata;
                            SPPU_REG_SEU_BRANCH_MASK:    seu_branch_mask    <= s_axi_wdata[15:0];
                            SPPU_REG_SEU_CONTEXT_ADDR:   seu_context_addr   <= s_axi_wdata;
                            SPPU_REG_SEU_EMBED_ADDR:     seu_embed_addr     <= s_axi_wdata;
                            default: ;
                        endcase
                    end
                end
                2'd2: begin
                    aw_done <= 1'b1;
                    aw_state <= 2'd0;
                end
            endcase
        end
    end

    // Read channel FSM
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ar_state <= 2'd0;
            ar_done  <= 1'b0;
            ar_rdata <= 32'd0;
        end else begin
            if (s_axi_rready)
                ar_done <= 1'b0;

            case (ar_state)
                2'd0: begin
                    if (s_axi_arvalid) begin
                        ar_state <= 2'd1;
                        case (s_axi_araddr)
                            SPPU_REG_CTRL:           ar_rdata <= ctrl;
                            SPPU_REG_STATUS:         ar_rdata <= status;
                            SPPU_REG_VEC_COUNT:      ar_rdata <= vec_count;
                            SPPU_REG_DIMENSION:      ar_rdata <= dimension;
                            SPPU_REG_TARGET_ADDR:    ar_rdata <= target_addr;
                            SPPU_REG_RESULT_IDX:     ar_rdata <= result_idx;
                            SPPU_REG_RESULT_SCORE:   ar_rdata <= result_score;
                            SPPU_REG_DEVICE_ID:      ar_rdata <= SPPU_DEVICE_VERSION;
                            SPPU_REG_INT_MASK:       ar_rdata <= int_mask;
                            SPPU_REG_INT_STATUS:     ar_rdata <= int_status_io;
                            SPPU_REG_SEU_CTRL:       ar_rdata <= seu_ctrl;
                            SPPU_REG_SEU_STATUS:     ar_rdata <= seu_status;
                            SPPU_REG_SEU_DEPTH:      ar_rdata <= seu_depth;
                            SPPU_REG_SEU_OFFSET:     ar_rdata <= seu_offset;
                            SPPU_REG_SEU_TREE_ADDR:  ar_rdata <= seu_tree_addr;
                            SPPU_REG_SEU_TREE_RES:   ar_rdata <= seu_tree_result;
                            SPPU_REG_SEU_PROB_BASE:  ar_rdata <= seu_prob_base;
                            SPPU_REG_SEU_IRQ_STATUS: ar_rdata <= seu_irq_status;
                            SPPU_REG_SEU_PROB_READ_IDX:  ar_rdata <= seu_prob_read_idx;
                            SPPU_REG_SEU_PROB_READBACK:  ar_rdata <= seu_prob_readback;
                            SPPU_REG_SEU_TREE_ENTRIES_TOTAL: ar_rdata <= seu_tree_entries_total;
                            // SEU v0.4 — speculative tree walker
                            SPPU_REG_SEU_TREE_CONFIG:     ar_rdata <= seu_tree_config;
                            SPPU_REG_SEU_NODE_BASE_ADDR:  ar_rdata <= seu_node_base_addr;
                            SPPU_REG_SEU_BRANCH_MASK:     ar_rdata <= {16'd0, seu_branch_mask};
                            SPPU_REG_SEU_TREE_RESULT_FLAGS: ar_rdata <= {16'd0, seu_tree_result_flags};
                            SPPU_REG_SEU_CONTEXT_ADDR:    ar_rdata <= seu_context_addr;
                            SPPU_REG_SEU_EMBED_ADDR:      ar_rdata <= seu_embed_addr;
                            SPPU_REG_SEU_BRANCH_BEST_IDX: ar_rdata <= seu_branch_best_idx;
                            SPPU_REG_SEU_BRANCH_BEST_SCORE: ar_rdata <= seu_branch_best_score;
                            12'h0FC:                 ar_rdata <= SPPU_MAGIC;
                            default:                 ar_rdata <= 32'd0;
                        endcase
                    end
                end
                2'd1: begin
                    ar_done <= 1'b1;
                    ar_state <= 2'd0;
                end
            endcase
        end
    end

endmodule
