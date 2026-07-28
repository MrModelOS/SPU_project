#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPGA_DIR="$PROJECT_DIR/fpga"
BUILD_DIR="$FPGA_DIR/build"
BITSTREAM="$BUILD_DIR/sppu_zynq7010.bit"

if [ ! -f "$BITSTREAM" ]; then
    echo "ERROR: Bitstream not found at $BITSTREAM"
    echo "Run scripts/build_fpga.sh first."
    exit 1
fi

echo "=== Programming FPGA via JTAG (openFPGALoader) ==="
echo "Bitstream: $BITSTREAM"
echo "Board:     digilent_a"

openFPGALoader -b digilent_a "$BITSTREAM" 2>&1

echo "=== FPGA programmed successfully ==="