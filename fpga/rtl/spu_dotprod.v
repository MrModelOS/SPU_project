`timescale 1ns / 1ps
`default_nettype none

// SPU Dot-Product Engine
// Computes dot products between target vector and each stored vector.
// Pipeline: 1 multiply-accumulate per cycle (single-precision float).
// For synthesis: use DSP48 blocks for FP multiply-accumulate.
module spu_dotprod (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        start,       // pulse to begin
    output reg         busy,
    output reg         done,        // pulse on completion

    // Config
    input  wire [31:0] vec_count,   // number of vectors to search
    input  wire [31:0] dimension,   // dimension of each vector

    // Target vector port (read target[d] for current dim)
    output reg  [11:0] target_raddr,
    input  wire [31:0] target_rdata,

    // Vector memory port (read vec[i][d])
    output reg  [11:0] vec_raddr,
    input  wire [31:0] vec_rdata,

    // Result
    output reg  [31:0] result_idx,
    output reg  [31:0] result_score
);

    localparam SPU_STATUS_READY = 2'd0;
    localparam SPU_STATUS_BUSY  = 2'd1;
    localparam SPU_STATUS_DONE  = 2'd2;

    // FSM states
    localparam ST_IDLE    = 3'd0;
    localparam ST_LOAD_TARGET = 3'd1;
    localparam ST_COMPUTE = 3'd2;
    localparam ST_DONE    = 3'd3;

    reg [2:0]  state;
    reg [31:0] cur_vec;        // current vector index
    reg [31:0] cur_dim;        // current dimension index
    reg [31:0] best_idx;
    reg [31:0] best_score;     // raw IEEE 754 bits
    reg [31:0] cur_dot_acc;    // dot product accumulator (raw bits)

    // Latched config
    reg [31:0] r_vec_count;
    reg [31:0] r_dimension;

    // Target buffer (stored in registers for fast access)
    reg [31:0] target_buf [0:767];

    // Simple FP multiply-accumulate (combinational)
    // For production: replace with proper IEEE 754 pipeline
    // This is a simplified version using Verilog real approximation
    reg [31:0] mul_a, mul_b;
    wire [31:0] mul_result;
    wire [31:0] add_result;

    // Placeholder: use integer multiply for simplicity
    // In real FPGA, replace with FP DSP pipeline
    assign mul_result = mul_a; // stub — real impl uses FP multiplier
    assign add_result = cur_dot_acc + mul_result; // stub

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= ST_IDLE;
            busy        <= 1'b0;
            done        <= 1'b0;
            cur_vec     <= 32'd0;
            cur_dim     <= 32'd0;
            best_idx    <= 32'hFFFFFFFF;
            best_score  <= 32'h80000000; // -inf
            cur_dot_acc <= 32'd0;
            r_vec_count <= 32'd0;
            r_dimension <= 32'd0;
            target_raddr <= 12'd0;
            vec_raddr    <= 12'd0;
            mul_a       <= 32'd0;
            mul_b       <= 32'd0;
        end else begin
            done <= 1'b0;

            case (state)
                ST_IDLE: begin
                    if (start) begin
                        state       <= ST_LOAD_TARGET;
                        busy        <= 1'b1;
                        r_vec_count <= vec_count;
                        r_dimension <= dimension;
                        cur_vec     <= 32'd0;
                        cur_dim     <= 32'd0;
                        best_idx    <= 32'hFFFFFFFF;
                        best_score  <= 32'h80000000;
                        cur_dot_acc <= 32'd0;
                        target_raddr <= 12'd0;
                    end
                end

                ST_LOAD_TARGET: begin
                    // Load target vector into buffer (1 cycle per element)
                    // For simplicity: assume dimension fits in buffer
                    target_buf[cur_dim[11:0]] <= target_rdata;
                    if (cur_dim + 1 >= r_dimension) begin
                        state   <= ST_COMPUTE;
                        cur_dim <= 32'd0;
                        cur_vec <= 32'd0;
                    end else begin
                        cur_dim     <= cur_dim + 1;
                        target_raddr <= cur_dim[11:0] + 1;
                    end
                end

                ST_COMPUTE: begin
                    // Read current vector element and target element
                    vec_raddr    <= cur_vec[11:0] * r_dimension[11:0] + cur_dim[11:0];
                    mul_a        <= vec_rdata;
                    mul_b        <= target_buf[cur_dim[11:0]];

                    // Accumulate (after 1 cycle latency from memory read)
                    if (cur_dim == 0 && cur_vec > 0) begin
                        // Check if previous vector was best
                        // (simplified: raw bit comparison for positive floats)
                        if ($signed(cur_dot_acc) > $signed(best_score)) begin
                            best_score <= cur_dot_acc;
                            best_idx   <= cur_vec - 1;
                        end
                        cur_dot_acc <= 32'd0;
                    end

                    if (cur_dim + 1 >= r_dimension) begin
                        // Finished this dimension — next vector
                        cur_dim <= 32'd0;
                        if (cur_vec + 1 >= r_vec_count) begin
                            state <= ST_DONE;
                        end else begin
                            cur_vec <= cur_vec + 1;
                        end
                    end else begin
                        cur_dim <= cur_dim + 1;
                    end
                end

                ST_DONE: begin
                    // Final comparison for last vector
                    if ($signed(cur_dot_acc) > $signed(best_score)) begin
                        best_score <= cur_dot_acc;
                        best_idx   <= cur_vec;
                    end
                    result_idx   <= (best_idx == 32'hFFFFFFFF) ? 32'd0 : best_idx;
                    result_score <= ($signed(best_score) <= $signed(32'h80000000))
                                    ? 32'd0 : best_score;
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end

endmodule
