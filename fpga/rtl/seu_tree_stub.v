`timescale 1ns / 1ps
`default_nettype none

module seu_tree (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        seu_reset,
    output reg         busy,
    output reg         done,
    input  wire [31:0] depth,
    input  wire [31:0] offset,
    input  wire [31:0] tree_addr,
    input  wire [31:0] prob_base,
    input  wire [31:0] tree_config,
    input  wire [31:0] node_base_addr,
    input  wire [15:0] branch_mask,
    input  wire [31:0] context_addr,
    input  wire [31:0] embed_addr,
    output reg  [11:0] vmem_c_addr,
    output reg  [31:0] vmem_c_wdata,
    output reg         vmem_c_wen,
    output reg  [11:0] vmem_d_addr,
    input  wire [31:0] vmem_d_rdata,
    output reg         embed_search_start,
    output reg  [31:0] embed_query_addr,
    output reg  [31:0] embed_vec_count,
    input  wire        embed_search_done,
    input  wire [31:0] embed_result_idx,
    input  wire [31:0] embed_result_score,
    output reg  [15:0] branch_valid,
    output reg  [31:0] branch_best_idx,
    output reg  [31:0] branch_best_score,
    output reg  [31:0] tree_status,
    output reg  [31:0] tree_count,
    output reg  [31:0] irq_seu,
    input  wire [31:0] prob_read_idx,
    output reg  [31:0] prob_readback,
    output reg  [31:0] entries_total
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            busy             <= 1'b0;
            done             <= 1'b0;
            vmem_c_addr      <= 12'd0;
            vmem_c_wdata     <= 32'd0;
            vmem_c_wen       <= 1'b0;
            vmem_d_addr      <= 12'd0;
            embed_search_start <= 1'b0;
            embed_query_addr <= 32'd0;
            embed_vec_count  <= 32'd0;
            branch_valid     <= 16'd0;
            branch_best_idx  <= 32'd0;
            branch_best_score<= 32'd0;
            tree_status      <= 32'd0;
            tree_count       <= 32'd0;
            irq_seu          <= 32'd0;
            prob_readback    <= 32'd0;
            entries_total    <= 32'd0;
        end else begin
            done <= 1'b0;
            irq_seu <= 32'd0;
            vmem_c_wen <= 1'b0;
            embed_search_start <= 1'b0;
            if (seu_reset) begin
                busy        <= 1'b0;
                tree_status <= 32'd0;
                branch_valid <= 16'd0;
            end else if (start) begin
                busy        <= 1'b1;
                tree_status <= 32'd1;
            end else if (busy) begin
                busy        <= 1'b0;
                done        <= 1'b1;
                tree_status <= 32'd2;
                tree_count  <= 32'd1;
                entries_total <= 32'd1;
                irq_seu     <= 32'h0000_0004;
            end
        end
    end

endmodule

`default_nettype wire
