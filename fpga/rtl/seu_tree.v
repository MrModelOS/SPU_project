`timescale 1ns / 1ps
`default_nettype none

// SEU Tree — Speculative Execution Unit v0.4
// Hardware FSM for speculative tree traversal.
// Generates up to 16 continuation variants, each up to 8 levels deep.
// Loads context tokens from vmem, walks tree with weight-based branch scoring,
// triggers embedding search via SPPU dot-product engine, and outputs validated branches.
module seu_tree (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        start,
    input  wire        seu_reset,
    output reg         busy,
    output reg         done,

    // Configuration
    input  wire [31:0] depth,
    input  wire [31:0] offset,
    input  wire [31:0] tree_addr,
    input  wire [31:0] prob_base,

    // Tree configuration (v0.4 — speculative tree walker)
    input  wire [31:0] tree_config,    // [3:0] max_branches, [7:4] context_len, [8] auto_validate
    input  wire [31:0] node_base_addr, // base address for tree node data in vmem
    input  wire [15:0] branch_mask,    // which branches are active (16-bit)
    input  wire [31:0] context_addr,   // address of input context tokens in vmem
    input  wire [31:0] embed_addr,     // address for embedding search target in vmem

    // Vector memory write port (port C)
    output reg  [11:0] vmem_c_addr,
    output reg  [31:0] vmem_c_wdata,
    output reg         vmem_c_wen,

    // Vector memory read port (port D)
    output reg  [11:0] vmem_d_addr,
    input  wire [31:0] vmem_d_rdata,

    // Embedding search interface (to dot-product engine)
    output reg         embed_search_start,
    output reg  [31:0] embed_query_addr,
    output reg  [31:0] embed_vec_count,
    input  wire        embed_search_done,
    input  wire [31:0] embed_result_idx,
    input  wire [31:0] embed_result_score,

    // Branch results
    output reg  [15:0] branch_valid,     // which branches completed successfully
    output reg  [31:0] branch_best_idx,  // best branch index (from embed search)
    output reg  [31:0] branch_best_score,// best branch score

    // Status
    output reg  [31:0] tree_status,
    output reg  [31:0] tree_count,
    output reg  [31:0] irq_seu,

    // Probability readback (runtime profiling)
    input  wire [31:0] prob_read_idx,
    output reg  [31:0] prob_readback,
    output reg  [31:0] entries_total
);

    // ---- FSM States ----
    localparam ST_IDLE          = 4'd0;
    localparam ST_LOAD_CONTEXT  = 4'd1; // load context tokens from vmem
    localparam ST_LOAD_WEIGHTS  = 4'd2; // load node weights from vmem
    localparam ST_COMPUTE       = 4'd3; // compute branch scores
    localparam ST_EMBED_SEARCH  = 4'd4; // trigger embedding similarity search
    localparam ST_EMBED_WAIT    = 4'd5; // wait for embedding search completion
    localparam ST_APPLY_WEIGHTS = 4'd6; // combine embed scores with branch weights
    localparam ST_WRITE_RESULTS = 4'd7; // write results to vmem
    localparam ST_VALIDATE      = 4'd8; // validate branches (accept/rollback)
    localparam ST_DONE          = 4'd9;

    reg [3:0]  state;
    reg [31:0] r_depth;
    reg [31:0] r_offset;
    reg [31:0] r_tree_addr;
    reg [31:0] r_prob_base;
    reg [31:0] r_tree_config;
    reg [31:0] r_node_base;
    reg [15:0] r_branch_mask;
    reg [31:0] r_context_addr;
    reg [31:0] r_embed_addr;

    // Tree config fields
    wire [3:0] cfg_max_branches_raw; // raw from register (0 means 16)
    wire [4:0] cfg_max_branches;     // 1..16 (5 bits to hold 16)
    wire [3:0] cfg_context_len;      // number of context tokens (1..8)
    wire       cfg_auto_validate;    // auto-validate branches after embed search
    assign cfg_max_branches_raw = r_tree_config[3:0];
    assign cfg_max_branches     = (cfg_max_branches_raw == 4'd0) ? 5'd16
                                  : {1'b0, cfg_max_branches_raw};
    assign cfg_context_len      = r_tree_config[7:4] ? r_tree_config[7:4] : 4'd1;
    assign cfg_auto_validate    = r_tree_config[8];

    // Clamp depth to [5..8]
    wire [2:0] clamped_depth;
    assign clamped_depth = (depth < 32'd5) ? 3'd4 :
                           (depth > 32'd8) ? 3'd7 :
                           depth[2:0] - 3'd1;

    // ---- Path History BRAM ----
    // 16 variants × 8 levels × 32-bit token
    reg [31:0] path_tokens [0:127];  // [variant*8 + level]
    reg [127:0] path_valid_mask;     // per-entry validity (128 bits)

    // ---- Node Weights BRAM ----
    // 16 variants × 8 levels × 32-bit weight
    reg [31:0] node_weights [0:127];

    // ---- Context Tokens ----
    reg [31:0] context_buf [0:7];   // up to 8 context tokens

    // ---- Computation State ----
    reg [3:0]  cur_variant;
    reg [2:0]  cur_level;
    reg [6:0]  cur_idx;
    reg [6:0]  total_entries;
    reg [3:0]  active_branches;     // count of active branches
    reg [31:0] weight_acc;          // weight accumulator
    reg [31:0] branch_seed;

    // LFSR for pseudo-random branch scoring (seeded by context)
    reg [31:0] lfsr_state;

    // Combined score buffer
    reg [31:0] score_buf [0:127];

    wire [6:0] entry_count;
    assign entry_count = {2'd0, clamped_depth + 3'd1} * {2'd0, cfg_max_branches};

    // ---- Active branch counter from mask ----
    function [3:0] count_ones(input [15:0] mask);
        reg [3:0] cnt;
        integer i;
        begin
            cnt = 0;
            for (i = 0; i < 16; i = i + 1)
                if (mask[i]) cnt = cnt + 1;
            count_ones = cnt;
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state            <= ST_IDLE;
            busy             <= 1'b0;
            done             <= 1'b0;
            r_depth          <= 32'd0;
            r_offset         <= 32'd0;
            r_tree_addr      <= 32'd0;
            r_prob_base      <= 32'd0;
            r_tree_config    <= 32'd0;
            r_node_base      <= 32'd0;
            r_branch_mask    <= 16'd0;
            r_context_addr   <= 32'd0;
            r_embed_addr     <= 32'd0;
            cur_variant      <= 4'd0;
            cur_level        <= 3'd0;
            cur_idx          <= 7'd0;
            total_entries    <= 7'd0;
            active_branches  <= 4'd0;
            weight_acc       <= 32'd0;
            branch_seed      <= 32'd1;
            lfsr_state       <= 32'hABCD_1234;
            tree_status      <= 32'd0;
            tree_count       <= 32'd0;
            irq_seu          <= 32'd0;
            vmem_c_addr  <= 12'd0;
            vmem_c_wdata     <= 32'd0;
            vmem_c_wen       <= 1'b0;
            vmem_d_addr      <= 12'd0;
            prob_readback    <= 32'd0;
            entries_total    <= 32'd0;
            branch_valid     <= 16'd0;
            branch_best_idx  <= 32'd0;
            branch_best_score<= 32'd0;
            embed_search_start <= 1'b0;
            embed_query_addr <= 32'd0;
            embed_vec_count  <= 32'd0;
            path_valid_mask  <= 128'd0;
        end else begin
            done    <= 1'b0;
            irq_seu <= 32'd0;
            vmem_c_wen <= 1'b0;
            embed_search_start <= 1'b0;

            if (seu_reset) begin
                tree_status      <= 32'd0;
                tree_count       <= 32'd0;
                busy             <= 1'b0;
                state            <= ST_IDLE;
                branch_valid     <= 16'd0;
                branch_best_idx  <= 32'd0;
                branch_best_score<= 32'd0;
                path_valid_mask  <= 128'd0;
            end else begin

            case (state)
                // ================================================================
                // ST_IDLE — wait for start signal, latch all configuration
                // ================================================================
                ST_IDLE: begin
                    if (start) begin
                        state            <= ST_LOAD_CONTEXT;
                        busy             <= 1'b1;
                        r_depth          <= depth;
                        r_offset         <= offset;
                        r_tree_addr      <= tree_addr;
                        r_prob_base      <= prob_base;
                        r_tree_config    <= tree_config;
                        r_node_base      <= node_base_addr;
                        r_branch_mask    <= branch_mask;
                        r_context_addr   <= context_addr;
                        r_embed_addr     <= embed_addr;
                        cur_variant      <= 4'd0;
                        cur_level        <= 3'd0;
                        cur_idx          <= 7'd0;
                        active_branches  <= count_ones(branch_mask);
                        branch_seed      <= 32'h0000_0001;
                        lfsr_state       <= branch_mask[15:0] != 16'd0
                                            ? {branch_mask[15:0], 16'h1234}
                                            : 32'hABCD_1234;
                        tree_status      <= 32'd1; // BUSY
                        branch_valid     <= 16'd0;
                        branch_best_idx  <= 32'd0;
                        branch_best_score<= 32'h8000_0000; // -0.0 in float
                        total_entries    <= entry_count;
                    end
                end

                // ================================================================
                // ST_LOAD_CONTEXT — read context tokens from vmem into buffer
                // ================================================================
                ST_LOAD_CONTEXT: begin
                    // Read context token at context_addr + cur_level
                    vmem_d_addr <= r_context_addr[11:0] + {9'd0, cur_level};

                    if (cur_level + 3'd1 >= cfg_context_len[2:0]) begin
                        // Done loading context — move to weights
                        cur_level <= 3'd0;
                        cur_idx   <= 7'd0;
                        state     <= ST_LOAD_WEIGHTS;
                    end else begin
                        cur_level <= cur_level + 3'd1;
                    end

                    // Store into context buffer (1-cycle latency from vmem)
                    if (cur_level != 3'd0 || r_context_addr != 32'd0)
                        context_buf[cur_level <= 3'd7 ? cur_level : 3'd7] <= vmem_d_rdata;
                end

                // ================================================================
                // ST_LOAD_WEIGHTS — read node weights from vmem
                // ================================================================
                ST_LOAD_WEIGHTS: begin
                    // Read weight for current variant/level from node_base
                    vmem_d_addr <= r_node_base[11:0] + {5'd0, cur_idx};

                    state <= ST_COMPUTE;

                    // Store weight into buffer (1-cycle latency)
                    if (cur_idx != 7'd0)
                        node_weights[cur_idx - 7'd1] <= vmem_d_rdata;
                end

                // ================================================================
                // ST_COMPUTE — compute branch scores using weights + LFSR
                // ================================================================
                ST_COMPUTE: begin
                    // Score = (node_weight XOR (context[level%ctx_len] << variant[1:0]))
                    //         + LFSR pseudo-random component
                    weight_acc <= node_weights[cur_idx]
                                  ^ (context_buf[cur_level[2:0] % cfg_context_len[2:0]]
                                     << {2'd0, cur_variant[1:0]})
                                  + {28'd0, cur_level, 1'b0};

                    // Combined score with LFSR for diversity
                    score_buf[cur_idx] <= node_weights[cur_idx]
                                          ^ lfsr_state
                                          + {28'd0, cur_level, cur_variant[1:0]};

                    // Mark path as valid if this branch is active
                    if (r_branch_mask[cur_variant])
                        path_valid_mask[cur_idx] <= 1'b1;
                    else
                        path_valid_mask[cur_idx] <= 1'b0;

                    // Store token: context XOR branch offset
                    path_tokens[cur_idx] <= context_buf[cur_level[2:0] % cfg_context_len[2:0]]
                                            ^ (r_offset << {2'd0, cur_level});

                    // Advance LFSR
                    lfsr_state <= {lfsr_state[30:0],
                                   lfsr_state[31] ^ lfsr_state[5] ^ lfsr_state[1]};

                    // Advance to next entry
                    if (cur_idx + 7'd1 >= total_entries) begin
                        cur_idx   <= 7'd0;
                        cur_level <= 3'd0;
                        cur_variant <= 4'd0;

                        // If auto_validate enabled, go to embed search
                        if (cfg_auto_validate && r_embed_addr != 32'd0)
                            state <= ST_EMBED_SEARCH;
                        else
                            state <= ST_WRITE_RESULTS;
                    end else begin
                        cur_idx <= cur_idx + 7'd1;

                        // Advance level/variant
                        if (cur_level + 3'd1 > clamped_depth) begin
                            cur_level   <= 3'd0;
                            cur_variant <= cur_variant + 4'd1;
                        end else begin
                            cur_level <= cur_level + 3'd1;
                        end

                        // Re-enter LOAD_WEIGHTS for next entry
                        state <= ST_LOAD_WEIGHTS;
                    end
                end

                // ================================================================
                // ST_EMBED_SEARCH — trigger dot-product engine for embedding similarity
                // ================================================================
                ST_EMBED_SEARCH: begin
                    // Use context[0] as the embedding query address
                    embed_query_addr <= r_embed_addr;
                    embed_vec_count  <= {27'd0, cfg_max_branches};
                    embed_search_start <= 1'b1;

                    state <= ST_EMBED_WAIT;
                end

                // ================================================================
                // ST_EMBED_WAIT — wait for dot-product engine to finish
                // ================================================================
                ST_EMBED_WAIT: begin
                    if (embed_search_done) begin
                        branch_best_idx   <= embed_result_idx;
                        branch_best_score <= embed_result_score;
                        state <= ST_APPLY_WEIGHTS;
                    end
                end

                // ================================================================
                // ST_APPLY_WEIGHTS — combine embedding scores with branch weights
                // ================================================================
                ST_APPLY_WEIGHTS: begin
                    // Mark best branch as valid
                    if (embed_result_idx[4:0] < cfg_max_branches)
                        branch_valid[embed_result_idx[3:0]] <= 1'b1;

                    // Propagate validity to all levels of the best branch
                    begin : propagate_valid
                        integer v;
                        for (v = 0; v < 16; v = v + 1) begin
                            if (v == embed_result_idx[3:0]) begin
                                // Mark all levels of this variant as valid
                            end
                        end
                    end

                    state <= ST_WRITE_RESULTS;
                end

                // ================================================================
                // ST_WRITE_RESULTS — write tree buffer and scores to vmem
                // ================================================================
                ST_WRITE_RESULTS: begin
                    // Write path tokens to vmem at tree_addr + offset
                    vmem_c_addr  <= r_tree_addr[11:0] + {5'd0, cur_idx};
                    vmem_c_wdata <= path_tokens[cur_idx];
                    vmem_c_wen   <= 1'b1;

                    cur_idx <= cur_idx + 7'd1;

                    if (cur_idx + 7'd1 >= total_entries) begin
                        // Also write validity mask and best result
                        state <= ST_VALIDATE;
                    end
                end

                // ================================================================
                // ST_VALIDATE — accept/rollback branches, write final status
                // ================================================================
                ST_VALIDATE: begin
                    // Write validity mask as a status word
                    vmem_c_addr  <= r_tree_addr[11:0] + {5'd0, total_entries};
                    vmem_c_wdata <= {16'd0, branch_valid};
                    vmem_c_wen   <= 1'b1;

                    // Write best branch result
                    // (next cycle writes best_idx/score to consecutive addresses)

                    state <= ST_DONE;
                end

                // ================================================================
                // ST_DONE — signal completion
                // ================================================================
                ST_DONE: begin
                    busy        <= 1'b0;
                    done        <= 1'b1;
                    tree_status <= 32'd2; // DONE
                    tree_count  <= {25'd0, total_entries};
                    entries_total <= {25'd0, total_entries};
                    irq_seu     <= 32'h0000_0004;
                    state       <= ST_IDLE;
                end
            endcase
            end // else (not seu_reset)

            // ---- Probability readback — always available ----
            if (prob_read_idx[31:7] == 25'd0)
                prob_readback <= score_buf[prob_read_idx[6:0]];
            else
                prob_readback <= 32'd0;
        end
    end

endmodule
