#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPGA_DIR="$PROJECT_DIR/fpga"
BUILD_DIR="$FPGA_DIR/build"
CONST_DIR="$FPGA_DIR/constraints"
XDC_FILE="$CONST_DIR/sppu_zynq7010.xdc"

echo "=== SPPU FPGA Build (Yosys + nextpnr-xilinx + openFPGALoader) ==="
echo "Project: $PROJECT_DIR"
echo "Build:   $BUILD_DIR"
echo "Board:   AntMiner XC7Z010 v1.0 (xc7z010clg400-1)"
echo ""

mkdir -p "$BUILD_DIR"

echo "=== Step 1: Yosys Synthesis ==="
cd "$FPGA_DIR"
make synth

echo ""
echo "=== Step 2: nextpnr Place & Route ==="
make fasm

echo ""
echo "=== Step 3: FASM → BIT ==="
if command -v xc7frames2bit &>/dev/null; then
    make bit
else
    echo "WARNING: xc7frames2bit not found in PATH."
    echo "Build prjxray-tools to get it, or install 'fasm2bit' package."
    echo "FASM file is at: $BUILD_DIR/sppu_pynq_top.fasm"
    echo "You can convert it manually:"
    echo "  xc7frames2bit --part_file fpga/arch/xc7z010clg400-1/part.yaml \\"
    echo "    --part_name xc7z010clg400-1 --frm_file $BUILD_DIR/sppu_pynq_top.fasm \\"
    echo "    --output_file $BUILD_DIR/sppu_zynq7010.bit"
    exit 1
fi

echo ""
echo "=== Build complete ==="
echo "Bitstream: $BUILD_DIR/sppu_zynq7010.bit"