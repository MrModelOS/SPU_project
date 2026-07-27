`timescale 1ns / 1ps
`default_nettype none

// SPPU Vector Memory — Coherent Shared Pool (XC7Z010 optimized)
// Port A: write (DMA controller)
// Port B: read  (dot-product engine)
// Port C: write (SEU tree results)
// Port D: read  (SEU probability config)
//
// Memory layout (4K entries):
//   [0 .. 767]               — target vector
//   [768 .. 768+VEC*DIM-1]   — stored vectors
//   [0x300 .. 0x3FF]         — SEU tree / probability graph area
//
// Port arbitration: A and C share write; B and D share read.
// On collision the higher-priority port wins (DMA > SEU for writes,
// dot-product > SEU for reads).
module sppu_vecmem (
    input  wire        clk,

    // Port A: write (DMA controller)
    input  wire [11:0] a_addr,
    input  wire [31:0] a_wdata,
    input  wire        a_wen,

    // Port B: read (dot-product engine)
    input  wire [11:0] b_addr,
    output reg  [31:0] b_rdata,

    // Port C: write (SEU tree)
    input  wire [11:0] c_addr,
    input  wire [31:0] c_wdata,
    input  wire        c_wen,

    // Port D: read (SEU probability config)
    input  wire [11:0] d_addr,
    output reg  [31:0] d_rdata
);

    // XC7Z010 budget: 60 BRAM18Kb blocks
    // 4K x 32-bit = 8 BRAM18K blocks (13% of BRAM budget)
    // Supports ~500 vectors x 8-dim or ~125 vectors x 32-dim
    localparam DEPTH = 4 * 1024;

    reg [31:0] mem [0:DEPTH-1];

    // Write arbitration: port A (DMA) has priority over port C (SEU)
    always @(posedge clk) begin
        if (a_wen)
            mem[a_addr] <= a_wdata;
        else if (c_wen)
            mem[c_addr] <= c_wdata;
    end

    // Read port B: dot-product engine (1-cycle latency)
    always @(posedge clk) begin
        b_rdata <= mem[b_addr];
    end

    // Read port D: SEU probability config (1-cycle latency)
    always @(posedge clk) begin
        d_rdata <= mem[d_addr];
    end

endmodule
