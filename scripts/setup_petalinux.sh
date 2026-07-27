#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPGA_DIR="$PROJECT_DIR/fpga"
BUILD_DIR="$FPGA_DIR/build"
XSA="$BUILD_DIR/sppu_zynq7010.xsa"

PLNX_DIR="$PROJECT_DIR/petalinux"

if ! command -v petalinux-build &>/dev/null; then
    echo "ERROR: petalinux-build not found in PATH."
    echo "Install PetaLinux SDK: https://www.xilinx.com/support/download/index.html"
    echo "After installing, source the settings.sh and retry."
    exit 1
fi

if [ ! -f "$XSA" ]; then
    echo "ERROR: XSA not found at $XSA"
    echo "Run scripts/build_fpga.sh first."
    exit 1
fi

echo "=== Creating PetaLinux Project for SPPU ==="

petalinux-create -t project -n sppu_project --template zynq --force --pshwconfig "$XSA"

(
    cd "$PLNX_DIR/sppu_project"

    echo "--- Configuring kernel with SPPU driver ---"
    petalinux-config -c kernel

    echo "--- Configuring rootfs to include SPPU modules ---"
    petalinux-config -c rootfs

    cat >> "$PLNX_DIR/sppu_project/project-spec/meta-user/recipes-kernel/linux/linux-xlnx_git.bbappend" << 'BBEOF' 2>/dev/null || true
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
BBEOF

    echo "--- Copying kernel module ---"
    mkdir -p "$PLNX_DIR/sppu_project/project-spec/meta-user/recipes-kernel/sppu-driver"
    cp "$PROJECT_DIR/kernel_module/sppu_driver.c" "$PLNX_DIR/sppu_project/project-spec/meta-user/recipes-kernel/sppu-driver/"
    cp "$PROJECT_DIR/kernel_module/Makefile" "$PLNX_DIR/sppu_project/project-spec/meta-user/recipes-kernel/sppu-driver/"

    cat > "$PLNX_DIR/sppu_project/project-spec/meta-user/recipes-kernel/sppu-driver/sppu-driver_%.bb" << 'BBFILE'
SUMMARY = "SPPU kernel driver"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed19f2271d61"

SRC_URI = "file://sppu_driver.c \
           file://Makefile"

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${libdir}/modules/${KERNEL_VERSION}/extra/
    install -m 0644 sppu_driver.ko ${D}${libdir}/modules/${KERNEL_VERSION}/extra/
}
BBFILE

    echo "--- Building PetaLinux image ---"
    petalinux-build 2>&1 | tee "$BUILD_DIR/petalinux_build.log"

    echo "--- Creating SD card image ---"
    petalinux-create -t image --name sdimage --format boot --rootfs

    echo "=== PetaLinux build complete ==="
    echo "SD image: $PLNX_DIR/sppu_project/images/linux/sd_image.img"
    echo "Kernel:   $PLNX_DIR/sppu_project/images/linux/Image"
) || {
    echo "ERROR: PetaLinux build failed. Check $BUILD_DIR/petalinux_build.log"
    exit 1
}