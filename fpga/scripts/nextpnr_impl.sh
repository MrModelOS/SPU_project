#!/bin/bash
set -e

BUILD_DIR="$1"
XDC_FILE="$2"
CHIPDB_DIR="$3"
FASM_OUT="$4"
JSON_IN="$5"

if [ -z "$BUILD_DIR" ] || [ -z "$XDC_FILE" ] || [ -z "$CHIPDB_DIR" ] || [ -z "$FASM_OUT" ] || [ -z "$JSON_IN" ]; then
    echo "Usage: nextpnr_impl.sh <build_dir> <xdc_file> <chipdb_dir> <fasm_out> <json_in>"
    echo "  build_dir   - output directory"
    echo "  xdc_file    - XDC constraints file"
    echo "  chipdb_dir  - directory containing chipdb.bin for target device"
    echo "  fasm_out    - output fasm file path"
    echo "  json_in     - input JSON netlist from Yosys"
    exit 1
fi

nextpnr-xilinx \
    --chipdb "$CHIPDB_DIR/chipdb.bin" \
    --xdc "$XDC_FILE" \
    --json "$JSON_IN" \
    --fasm "$FASM_OUT" \
    --timing-allow-fail \
    --placer heap \
    --router router2 \
    2>&1 | tee "$BUILD_DIR/nextpnr.log"