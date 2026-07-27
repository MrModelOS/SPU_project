#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPGA_DIR="$PROJECT_DIR/fpga"
BUILD_DIR="$FPGA_DIR/build"
RTL_DIR="$FPGA_DIR/rtl"
XDC_FILE="$FPGA_DIR/constraints/sppu_zynq7010.xdc"

echo "=== SPPU FPGA Build for AntMiner XC7Z010 v1.0 ==="
echo "Project: $PROJECT_DIR"
echo "Build:   $BUILD_DIR"

mkdir -p "$BUILD_DIR"

RTL_SRC="$RTL_DIR/sppu_regs.v $RTL_DIR/sppu_pynq_top.v $RTL_DIR/sppu_top.v $RTL_DIR/sppu_dma.v $RTL_DIR/sppu_dotprod.v $RTL_DIR/sppu_vecmem.v $RTL_DIR/seu_tree.v"

vivado -mode batch -source "$FPGA_DIR/scripts/vivado_synth.tcl" \
    -tclargs "$RTL_SRC" "$XDC_FILE" "$BUILD_DIR" 2>&1 | tee "$BUILD_DIR/build.log"

echo "=== Build complete ==="
echo "Bitstream: $BUILD_DIR/sppu_zynq7010.bit"
echo "XSA:       $BUILD_DIR/sppu_zynq7010.xsa"