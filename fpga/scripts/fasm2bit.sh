#!/bin/bash
set -e

FASM_FILE="$1"
PART_YAML="$2"
PART_NAME="$3"
BIT_OUT="$4"

if [ -z "$FASM_FILE" ] || [ -z "$PART_YAML" ] || [ -z "$PART_NAME" ] || [ -z "$BIT_OUT" ]; then
    echo "Usage: fasm2bit.sh <fasm_file> <part_yaml> <part_name> <bit_output>"
    echo "  fasm_file   - FASM bitstream text from nextpnr"
    echo "  part_yaml   - path to part.yaml from prjxray-db"
    echo "  part_name   - FPGA part name (e.g. xc7z010clg400-1)"
    echo "  bit_output  - output binary bitstream path"
    exit 1
fi

if [ ! -f "$FASM_FILE" ]; then
    echo "ERROR: FASM file not found at $FASM_FILE"
    exit 1
fi

if [ ! -f "$PART_YAML" ]; then
    echo "ERROR: part.yaml not found at $PART_YAML"
    exit 1
fi

echo "=== Converting FASM to BIT stream ==="
echo "FASM:    $FASM_FILE"
echo "Part:    $PART_NAME"
echo "Output:  $BIT_OUT"

xc7frames2bit \
    --part_file "$PART_YAML" \
    --part_name "$PART_NAME" \
    --frm_file "$FASM_FILE" \
    --output_file "$BIT_OUT" 2>&1

echo "=== BIT stream ready: $BIT_OUT ==="