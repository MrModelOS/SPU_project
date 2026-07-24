#!/bin/bash
# qemu-spu.sh — Launch QEMU with SPU virtual PCI device
#
# Prerequisites:
#   - QEMU compiled with SPU device (see qemu/README.md)
#   - Linux kernel image for the guest
#   - Root filesystem image (optional)
#
# Usage:
#   ./qemu-spu.sh [--kernel /path/to/bzImage] [--rootfs /path/to/rootfs.img]

set -e

KERNEL=""
ROOTFS=""
MEMORY="2G"
SMP="2"
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --kernel)  KERNEL="$2"; shift 2 ;;
        --rootfs)  ROOTFS="$2"; shift 2 ;;
        --memory)  MEMORY="$2"; shift 2 ;;
        --smp)     SMP="$2";    shift 2 ;;
        --debug)   EXTRA_ARGS="$EXTRA_ARGS -s -S"; shift ;;
        --append)  EXTRA_ARGS="$EXTRA_ARGS -append \"$2\""; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--kernel bzImage] [--rootfs rootfs.img] [--memory 2G] [--smp 2]"
            echo ""
            echo "Options:"
            echo "  --kernel    Path to Linux kernel image (bzImage)"
            echo "  --rootfs    Path to root filesystem image"
            echo "  --memory    Guest memory (default: 2G)"
            echo "  --smp       Number of CPUs (default: 2)"
            echo "  --debug     Start with GDB stub (-s -S)"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Build QEMU command
CMD="qemu-system-x86_64"
CMD="$CMD -m $MEMORY"
CMD="$CMD -smp $SMP"
CMD="$CMD -enable-kvm"
CMD="$CMD -machine q35"
CMD="$CMD -nographic"

# Add SPU PCI device
CMD="$CMD -device spu-pci"

# Add kernel if specified
if [ -n "$KERNEL" ]; then
    CMD="$CMD -kernel $KERNEL"
    CMD="$CMD -append \"console=ttyS0 root=/dev/sda rw\""
fi

# Add rootfs if specified
if [ -n "$ROOTFS" ]; then
    CMD="$CMD -drive file=$ROOTFS,format=raw,if=virtio"
fi

# Add kernel modules (SPU driver + SDK)
CMD="$CMD -virtfs local,path=$(dirname "$0")/../kernel_module,model=9p,readonly=off,mount_tag=spu_module"
CMD="$CMD -virtfs local,path=$(dirname "$0")/../sdk,model=9p,readonly=off,mount_tag=spu_sdk"
CMD="$CMD -virtfs local,path=$(dirname "$0")/../tools,model=9p,readonly=off,mount_tag=spu_tools"

CMD="$CMD $EXTRA_ARGS"

echo "=== QEMU SPU ==="
echo "Command: $CMD"
echo ""
echo "Inside guest, mount shared folders:"
echo "  mount -t 9p -o trans=virtio spu_module /mnt/module"
echo "  mount -t 9p -o trans=virtio spu_sdk /mnt/sdk"
echo "  mount -t 9p -o trans=virtio spu_tools /mnt/tools"
echo ""
echo "Then load the SPU driver:"
echo "  insmod /mnt/module/spu_driver.ko"
echo ""
echo "Run the SDK demo:"
echo "  cd /mnt/sdk && make && ./examples/spu_demo"
echo ""

eval $CMD
