# SPPU QEMU PCI Device
#
# Two build modes:
#   make          - build as QEMU source tree module
#   make external - build as external shared library (for -device loader)

QEMU_DIR  ?= /usr/src/qemu
QEMU_OBJ  ?= $(QEMU_DIR)/build

# For in-tree build (requires QEMU source)
.PHONY: all clean

all:
	@echo "=== SPPU QEMU Device ==="
	@echo ""
	@echo "To build this device, copy sppu_qemu.c into QEMU source tree:"
	@echo ""
	@echo "  1. cp sppu_qemu.c $(QEMU_DIR)/hw/misc/"
	@echo "  2. Add to $(QEMU_DIR)/hw/misc/Kconfig:"
	@echo "     config SPPU_PCI"
	@echo "         bool \"SPPU Search Processing Unit\""
	@echo "         default y"
	@echo "         depends on PCI"
	@echo ""
	@echo "  3. Add to $(QEMU_DIR)/hw/misc/Makefile.objs:"
	@echo "     obj-\$(CONFIG_SPPU_PCI) += sppu_qemu.o"
	@echo ""
	@echo "  4. Rebuild QEMU:"
	@echo "     cd $(QEMU_OBJ) && make -j\$$(nproc)"
	@echo ""
	@echo "Then run with:"
	@echo "  qemu-system-x86_64 ... -device sppu-pci"
	@echo ""
	@echo "Or use the convenience script:"
	@echo "  ./qemu-sppu.sh"

clean:
	rm -f *.o *.so *.ko
