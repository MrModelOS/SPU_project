#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPGA_DIR="$PROJECT_DIR/fpga"
BUILD_DIR="$FPGA_DIR/build"
XSA="$BUILD_DIR/sppu_zynq7010.xsa"

BOOT_DIR="$BUILD_DIR/boot"
mkdir -p "$BOOT_DIR"

if [ ! -f "$XSA" ]; then
    echo "ERROR: XSA not found at $XSA"
    echo "Run scripts/build_fpga.sh first."
    exit 1
fi

echo "=== Building boot image for AntMiner XC7Z010 ==="

# Check for PetaLinux or Vivado SDK
if command -v petalinux-build &>/dev/null; then
    echo "PetaLinux found, using petalinux-build flow."

    PLNX_DIR="$PROJECT_DIR/petalinux"
    if [ ! -d "$PLNX_DIR/project-spec" ]; then
        echo "Creating new PetaLinux project..."
        petalinux-create -t project -n sppu_project --template zynq \
            --pshwconfig "$XSA" 2>&1 || true

        if [ ! -d "$PLNX_DIR/project-spec" ]; then
            echo "ERROR: Failed to create PetaLinux project."
            echo "Place the XSA at: $PLNX_DIR/sppu_zynq7010.xsa"
            echo "Then run: petalinux-config / petalinux-build"
            exit 1
        fi
    fi

    (cd "$PLNX_DIR" && petalinux-build 2>&1 | tee "$BOOT_DIR/petalinux_build.log")

    cp "$PLNX_DIR/images/linux/sd_image.img" "$BOOT_DIR/sd_image.img" 2>/dev/null || true
    cp "$PLNX_DIR/images/linux/Image" "$BOOT_DIR/Image" 2>/dev/null || true
    cp "$PLNX_DIR/images/linux/devicetree.dtb" "$BOOT_DIR/devicetree.dtb" 2>/dev/null || true

elif command -v xsct &>/dev/null; then
    echo "Vivado SDK (xsct) found, using SDK bootgen flow."

    XSCT_DIR="$PROJECT_DIR/sdk"
    if [ ! -d "$XSCT_DIR" ]; then
        echo "Creating SDK project structure..."
        mkdir -p "$XSCT_DIR/fsbl/src"
    fi

    # Generate FSBL source
    cat > "$XSCT_DIR/fsbl/Makefile" << 'MAKEEOF'
all:
	xsct -f build_fsbl.tcl
MAKEEOF

    # Export hardware from Vivado (if not already done)
    echo "Export hardware platform from Vivado IDE if needed."
    echo "Import XSA: File -> Import -> Hardware Platform ($XSA)"

    # Bootgen command for Zynq-7010
    cat > "$BOOT_DIR/boot.bif" << 'BIFEOF'
the_ROM_image:
{
  [bootloader] $BOOT_DIR/fsbl.elf
  $BUILD_DIR/sppu_zynq7010.bit
}
BIFEOF

    bootgen -image "$BOOT_DIR/boot.bif" -o "$BOOT_DIR/boot.bin" -w 2>&1 | tee "$BOOT_DIR/bootgen.log" || true

else
    echo "WARNING: Neither PetaLinux nor Vivado SDK (xsct) found."
    echo "Falling back to manual SD card build instructions."

    cat > "$BOOT_DIR/MANUAL_INSTRUCTIONS.txt" << 'MANEOF'
=== Manual SD Card Boot Setup ===

1. Build FSBL using Vivado SDK or Vitis:
   - Import XSA: $XSA
   - Create application: Zynq FSBL template
   - Export: fsbl.elf

2. Copy Zynq Boot Gen binary to: $BOOT_DIR/fsbl.elf

3. Create boot.bif in: $BOOT_DIR/boot.bif

the_ROM_image:
{
  [bootloader] $BOOT_DIR/fsbl.elf
  $BUILD_DIR/sppu_zynq7010.bit
}

4. Run bootgen:
   bootgen -image $BOOT_DIR/boot.bif -o $BOOT_DIR/boot.bin -w

5. Write boot.bin to SD card (first sector):
   sudo dd if=$BOOT_DIR/boot.bin of=/dev/sdX bs=512 seek=0 conv=notrunc

6. Copy kernel (Image), device tree (.dtb), and rootfs to SD card:
   - /boot/Image
   - /boot/devicetree.dtb
   - /boot/platform.dtb -> copy from PetaLinux or XSA export
   - rootfs.tar.gz at root of SD card

7. Insert SD card into AntMiner XC7Z010 and power on.
MANEOF
fi

echo "=== Boot image ready ==="
echo "Boot directory: $BOOT_DIR"
ls -la "$BOOT_DIR"