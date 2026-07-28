#!/bin/bash
set -e

BIT_FILE="$1"
BOARD_NAME="${2:-digilent_a}"

if [ -z "$BIT_FILE" ]; then
    echo "Usage: program_fpga.sh <bit_file> [board_name]"
    echo "  bit_file  - path to .bit bitstream file"
    echo "  board_name - openFPGALoader board name (default: digilent_a)"
    exit 1
fi

if [ ! -f "$BIT_FILE" ]; then
    echo "ERROR: Bitstream not found at $BIT_FILE"
    exit 1
fi

echo "=== Programming FPGA via JTAG ==="
echo "Bitstream: $BIT_FILE"
echo "Board: $BOARD_NAME"

openFPGALoader -b "$BOARD_NAME" "$BIT_FILE" 2>&1

echo "=== FPGA programmed successfully ==="