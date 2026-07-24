`timescale 1ns / 1ps
`default_nettype none

// SPU Vector Memory
// Dual-port BRAM: port A for DMA writes, port B for dot-product reads.
// Depth: 1024 x 768 = 786432 entries (3MB) — use BRAM primitives on FPGA.
module spu_vecmem (
    input  wire        clk,

    // Port A: write (DMA controller)
    input  wire [17:0] a_addr,
    input  wire [31:0] a_wdata,
    input  wire        a_wen,

    // Port B: read (dot-product engine)
    input  wire [17:0] b_addr,
    output reg  [31:0] b_rdata
);

    // Total entries: SPU_MAX_VECTORS(1000) * SPU_MAX_DIMENSION(768) + 768 (target)
    // Use 2^18 = 262144 entries — target at [0..767], vectors at [768..]
    localparam DEPTH = 18'h40000; // 256K entries for synthesis fit

    reg [31:0] mem [0:DEPTH-1];

    always @(posedge clk) begin
        if (a_wen)
            mem[a_addr] <= a_wdata;
    end

    always @(posedge clk) begin
        b_rdata <= mem[b_addr];
    end

endmodule
