#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPGA_DIR="$PROJECT_DIR/fpga"
BUILD_DIR="$FPGA_DIR/build"
XSA="$BUILD_DIR/sppu_zynq7010.xsa"
BOOT_DIR="$BUILD_DIR/boot"

mkdir -p "$BOOT_DIR"

if [ ! -f "$BUILD_DIR/sppu_zynq7010.bit" ]; then
    echo "ERROR: Bitstream not found at $BUILD_DIR/sppu_zynq7010.bit"
    echo "Run scripts/build_fpga.sh first."
    exit 1
fi

echo "=== Creating boot image for AntMiner XC7Z010 ==="

if command -v bootgen &>/dev/null; then
    cat > "$BOOT_DIR/boot.bif" << 'BIFEOF'
the_ROM_image:
{
  [bootloader] $BOOT_DIR/fsbl.elf
  $BUILD_DIR/sppu_zynq7010.bit
}
BIFEOF

    bootgen -image "$BOOT_DIR/boot.bif" -o "$BOOT_DIR/boot.bin" -w 2>&1 | tee "$BOOT_DIR/bootgen.log" || true
    echo "=== Boot image ready: $BOOT_DIR/boot.bin ==="
else
    echo "WARNING: bootgen not found in PATH."
    echo "Open-source alternative: build FSBL with Xilinx SDK/Vitis or use bare-metal elf + openFPGALoader."
    echo ""
    echo "For SD card boot, create boot.bin manually:"
    echo "  1. Build FSBL from Xilinx SDK (or use a prebuilt one)"
    echo "  2. Concatenate FSBL + bitstream:"
    echo "     cat fsbl.elf sppu_zynq7010.bit > boot.bin"
    echo "  3. Write boot.bin to SD card (first sector)"
    echo "  4. Copy kernel Image, dtb, rootfs to SD card"
    echo ""
    echo "Alternatively, for JTAG-only programming, just flash the bitstream directly:"
    echo "  scripts/flash_jtag.sh"
fi

echo "=== Boot directory: $BOOT_DIR ==="
ls -la "$BOOT_DIR"