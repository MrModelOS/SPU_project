`timescale 1ns / 1ps
`default_nettype none

// SEU Tree — Speculative Execution Unit
// Generates 16 continuation variants, each 5–8 steps deep.
// Produces probability scores for every branch of the prediction tree.
// Runs in parallel with spu_dotprod via shared vector memory port.
module seu_tree (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        start,
    input  wire        seu_reset,    // SEU-specific reset (clears status)
    output reg         busy,
    output reg         done,

    // Configuration
    input  wire [31:0] depth,       // tree depth (5..8)
    input  wire [31:0] offset,      // branch offsets (packed)
    input  wire [31:0] tree_addr,   // base address in vmem for tree storage
    input  wire [31:0] prob_base,   // base address for probability config

    // Vector memory write port (port C — dedicated to SEU)
    output reg  [17:0] vmem_c_addr,
    output reg  [31:0] vmem_c_wdata,
    output reg         vmem_c_wen,

    // Vector memory read port (read probability config)
    output reg  [17:0] vmem_d_addr,
    input  wire [31:0] vmem_d_rdata,

    // Status
    output reg  [31:0] tree_status,  // result status word
    output reg  [31:0] tree_count,   // total entries computed
    output reg  [31:0] irq_seu,      // SEU interrupt line

    // Probability readback (for runtime profiling)
    input  wire [31:0] prob_read_idx,  // index to read (0..127)
    output reg  [31:0] prob_readback,  // probability value at idx
    output reg  [31:0] entries_total   // total entries computed
);

    // ---- FSM States ----
    localparam ST_IDLE     = 3'd0;
    localparam ST_LOAD     = 3'd1; // load prob config from vmem
    localparam ST_COMPUTE  = 3'd2; // compute scores for each variant
    localparam ST_WRITE    = 3'd3; // write results to vmem
    localparam ST_DONE     = 3'd4;

    reg [2:0]  state;
    reg [31:0] r_depth;        // latched depth (clamped 5..8)
    reg [31:0] r_offset;
    reg [31:0] r_tree_addr;
    reg [31:0] r_prob_base;

    // Tree: 16 variants x 8 levels = 128 entries
    // Each entry: probability score (raw IEEE 754 float bits)
    reg [31:0] tree_buf [0:127];

    // Current computation position
    reg [3:0]  cur_variant;    // 0..15
    reg [2:0]  cur_level;      // 0..7 (max depth-1)
    reg [6:0]  cur_idx;        // flat index = variant * 8 + level
    reg [6:0]  total_entries;  // total entries to process

    // Probability accumulator
    reg [31:0] prob_acc;
    reg [31:0] branch_seed;

    // Clamp depth to [5..8]
    wire [2:0] clamped_depth;
    assign clamped_depth = (depth < 32'd5) ? 3'd4 :       // depth=5 => 5 levels
                           (depth > 32'd8) ? 3'd7 :        // depth=8 => 8 levels
                           depth[2:0] - 3'd1;              // convert to 0-based

    wire [6:0] entry_count;
    assign entry_count = {3'd0, clamped_depth + 3'd1} * 7'd16;   // variants * depth

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= ST_IDLE;
            busy        <= 1'b0;
            done        <= 1'b0;
            r_depth     <= 32'd0;
            r_offset    <= 32'd0;
            r_tree_addr <= 32'd0;
            r_prob_base <= 32'd0;
            cur_variant <= 4'd0;
            cur_level   <= 3'd0;
            cur_idx     <= 7'd0;
            total_entries <= 7'd0;
            prob_acc    <= 32'd0;
            branch_seed <= 32'd1;
            tree_status <= 32'd0;
            tree_count  <= 32'd0;
            irq_seu     <= 32'd0;
            vmem_c_addr  <= 18'd0;
            vmem_c_wdata <= 32'd0;
            vmem_c_wen   <= 1'b0;
            vmem_d_addr  <= 18'd0;
            prob_readback <= 32'd0;
            entries_total <= 32'd0;
        end else begin
            done    <= 1'b0;
            irq_seu <= 32'd0;
            vmem_c_wen <= 1'b0;

            // SEU-specific reset clears status
            if (seu_reset) begin
                tree_status <= 32'd0; // READY
                tree_count  <= 32'd0;
                busy        <= 1'b0;
                state       <= ST_IDLE;
            end else begin

            case (state)
                ST_IDLE: begin
                    if (start) begin
                        state       <= ST_LOAD;
                        busy        <= 1'b1;
                        r_depth     <= depth;
                        r_offset    <= offset;
                        r_tree_addr <= tree_addr;
                        r_prob_base <= prob_base;
                        cur_variant <= 4'd0;
                        cur_level   <= 3'd0;
                        cur_idx     <= 7'd0;
                        total_entries <= entry_count;
                        prob_acc    <= 32'd0;
                        branch_seed <= 32'h0000_0001;
                        tree_status <= 32'd1; // BUSY
                    end
                end

                ST_LOAD: begin
                    // Read probability configuration from vmem
                    // prob_base + cur_idx gives the seed/weight for this branch
                    vmem_d_addr <= r_prob_base[17:0] + {11'd0, cur_idx};

                    // Move to computation after read latency
                    state <= ST_COMPUTE;
                end

                ST_COMPUTE: begin
                    // Generate probability score for current variant/level
                    // Uses a simple LFSR-like PRNG seeded by branch offsets
                    // and modulated by depth position.
                    //
                    // prob = branch_seed XOR (offset << level) + depth_weight
                    prob_acc <= branch_seed ^ (r_offset << cur_level[1:0])
                                + {28'd0, cur_level, 1'b0};

                    // Shift seed for next iteration (pseudo-random)
                    branch_seed <= {branch_seed[30:0], branch_seed[31] ^ branch_seed[5]};

                    // Store into tree buffer
                    tree_buf[cur_idx] <= prob_acc;

                    // Advance to next entry
                    if (cur_idx + 7'd1 >= total_entries) begin
                        // All entries computed — write results to vmem
                        cur_idx   <= 7'd0;
                        state     <= ST_WRITE;
                    end else begin
                        cur_idx   <= cur_idx + 7'd1;
                        cur_level <= (cur_level + 3'd1 > clamped_depth) ? 3'd0
                                     : cur_level + 3'd1;
                        if (cur_level + 3'd1 > clamped_depth)
                            cur_variant <= cur_variant + 4'd1;

                        // Re-enter LOAD to read next config entry
                        state <= ST_LOAD;
                    end
                end

                ST_WRITE: begin
                    // Write tree buffer to vmem at tree_addr + offset
                    vmem_c_addr  <= r_tree_addr[17:0] + {11'd0, cur_idx};
                    vmem_c_wdata <= tree_buf[cur_idx];
                    vmem_c_wen   <= 1'b1;

                    cur_idx <= cur_idx + 7'd1;

                    if (cur_idx + 7'd1 >= total_entries) begin
                        state <= ST_DONE;
                    end
                end

                ST_DONE: begin
                    busy       <= 1'b0;
                    done       <= 1'b1;
                    tree_status <= 32'd2; // DONE
                    tree_count  <= {25'd0, total_entries};
                    entries_total <= {25'd0, total_entries};
                    irq_seu    <= 32'h0000_0004; // SEU interrupt bit
                    state      <= ST_IDLE;
                end
            endcase
            end // else (not seu_reset)

            // Probability readback — always available
            if (prob_read_idx[31:7] == 25'd0)
                prob_readback <= tree_buf[prob_read_idx[6:0]];
            else
                prob_readback <= 32'd0;
        end
    end

endmodule
