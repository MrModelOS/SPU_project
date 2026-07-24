/*
 * spu_qemu.c — QEMU virtual PCI device: Search Processing Unit
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_regs.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define SPU_REG_CTRL         0x00
#define SPU_REG_STATUS       0x04
#define SPU_REG_VEC_COUNT    0x08
#define SPU_REG_DIMENSION    0x0C
#define SPU_REG_RESULT_IDX   0x18
#define SPU_REG_RESULT_SCORE 0x1C
#define SPU_REG_DEVICE_ID    0x20
#define SPU_REG_INT_MASK     0x24
#define SPU_REG_INT_STATUS   0x28
#define SPU_REG_MAGIC        0xFC

#define SPU_CTRL_START   (1U << 0)
#define SPU_CTRL_RESET   (1U << 1)
#define SPU_CTRL_INT_EN  (1U << 2)

#define SPU_STATUS_READY  0x00
#define SPU_STATUS_BUSY   0x01
#define SPU_STATUS_DONE   0x02

#define SPU_INT_COMPLETE  (1U << 0)

#define SPU_PCI_VENDOR_ID  0x1234
#define SPU_PCI_DEVICE_ID  0x5780
#define SPU_DEVICE_VERSION 0x0001
#define SPU_MAGIC_VALUE    0x53505520

#define SPU_MAX_VECTORS   1000
#define SPU_MAX_DIMENSION 768
#define SPU_BAR0_SIZE     0x1000
#define SPU_VEC_MEM_SIZE  (SPU_MAX_VECTORS * SPU_MAX_DIMENSION * (int)sizeof(float))

#define TYPE_SPU_PCI_DEVICE "spu-pci"
OBJECT_DECLARE_SIMPLE_TYPE(SPUPCIState, SPU_PCI_DEVICE)

struct SPUPCIState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion vector_mem;
    uint32_t regs[16];
    float *vec_bank;
    float target[SPU_MAX_DIMENSION];
};

static void spu_run_search(SPUPCIState *s)
{
    uint32_t count = s->regs[SPU_REG_VEC_COUNT / 4];
    uint32_t dim   = s->regs[SPU_REG_DIMENSION / 4];
    if (count > SPU_MAX_VECTORS) count = SPU_MAX_VECTORS;
    if (dim > SPU_MAX_DIMENSION) dim = SPU_MAX_DIMENSION;

    s->regs[SPU_REG_STATUS / 4] = SPU_STATUS_BUSY;

    int best_index = -1;
    float max_score = -1e9f;

    for (uint32_t i = 0; i < count; i++) {
        float dot = 0.0f;
        float *vec = s->vec_bank + (size_t)i * SPU_MAX_DIMENSION;
        for (uint32_t d = 0; d < dim; d++) {
            dot += vec[d] * s->target[d];
        }
        if (dot > max_score) {
            max_score = dot;
            best_index = (int)i;
        }
    }

    s->regs[SPU_REG_RESULT_IDX / 4] = (uint32_t)best_index;
    memcpy(&s->regs[SPU_REG_RESULT_SCORE / 4], &max_score, sizeof(float));
    s->regs[SPU_REG_STATUS / 4] = SPU_STATUS_DONE;

    if (s->regs[SPU_REG_INT_MASK / 4] & SPU_CTRL_INT_EN) {
        s->regs[SPU_REG_INT_STATUS / 4] |= SPU_INT_COMPLETE;
        pci_set_irq(&s->parent_obj, 1);
    }

    printf("[SPU-QEMU] Search: %u vec dim=%u best=#%d score=%.4f\n",
           count, dim, best_index, max_score);
}

static uint64_t spu_mmio_read(void *opaque, hwaddr addr, unsigned int size)
{
    SPUPCIState *s = SPU_PCI_DEVICE(opaque);
    if (addr + size > SPU_BAR0_SIZE) return 0;
    if (addr == SPU_REG_DEVICE_ID) return SPU_DEVICE_VERSION;
    if (addr == SPU_REG_MAGIC) return SPU_MAGIC_VALUE;
    return s->regs[addr / 4];
}

static void spu_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned int size)
{
    SPUPCIState *s = SPU_PCI_DEVICE(opaque);
    if (addr + size > SPU_BAR0_SIZE) return;

    switch (addr) {
    case SPU_REG_CTRL:
        if (val & SPU_CTRL_RESET) {
            memset(s->regs, 0, sizeof(s->regs));
            s->regs[SPU_REG_STATUS / 4] = SPU_STATUS_READY;
            printf("[SPU-QEMU] Reset\n");
        }
        if ((val & SPU_CTRL_START) &&
            (s->regs[SPU_REG_STATUS / 4] == SPU_STATUS_READY ||
             s->regs[SPU_REG_STATUS / 4] == SPU_STATUS_DONE)) {
            spu_run_search(s);
        }
        s->regs[SPU_REG_CTRL / 4] = (uint32_t)val;
        break;
    case SPU_REG_INT_STATUS:
        if (val & SPU_INT_COMPLETE) {
            s->regs[SPU_REG_INT_STATUS / 4] &= ~SPU_INT_COMPLETE;
            pci_set_irq(&s->parent_obj, 0);
        }
        break;
    default:
        s->regs[addr / 4] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps spu_mmio_ops = {
    .read = spu_mmio_read,
    .write = spu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps spu_vec_mem_ops = {
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void spu_realize(PCIDevice *pdev, Error **errp)
{
    SPUPCIState *s = SPU_PCI_DEVICE(pdev);

    pci_set_word(pdev->config + PCI_VENDOR_ID, SPU_PCI_VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, SPU_PCI_DEVICE_ID);
    pdev->config[PCI_REVISION_ID] = 0x01;
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pdev->config[PCI_INTERRUPT_PIN] = 1;

    s->vec_bank = g_malloc0(SPU_VEC_MEM_SIZE);
    if (!s->vec_bank) {
        error_setg(errp, "spu: allocation failed");
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &spu_mmio_ops, s,
                          "spu-mmio", SPU_BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    memory_region_init_io(&s->vector_mem, OBJECT(s), &spu_vec_mem_ops, s,
                          "spu-vec-mem", SPU_VEC_MEM_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->vector_mem);

    s->regs[SPU_REG_STATUS / 4] = SPU_STATUS_READY;
    printf("[SPU-QEMU] Ready: BAR0=%uKB BAR1=%dMB\n",
           SPU_BAR0_SIZE / 1024, SPU_VEC_MEM_SIZE / (1024 * 1024));
}

static void spu_unrealize(PCIDevice *pdev)
{
    SPUPCIState *s = SPU_PCI_DEVICE(pdev);
    g_free(s->vec_bank);
}

static const VMStateDescription spu_vmstate = {
    .name = TYPE_SPU_PCI_DEVICE,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, SPUPCIState, 16),
        VMSTATE_END_OF_LIST()
    }
};

static void spu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = spu_realize;
    k->exit = spu_unrealize;
    k->vendor_id = SPU_PCI_VENDOR_ID;
    k->device_id = SPU_PCI_DEVICE_ID;

    dc->desc = "SPU - Search Processing Unit";
    dc->vmsd = &spu_vmstate;
}

static const TypeInfo spu_info = {
    .name          = TYPE_SPU_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SPUPCIState),
    .class_init    = spu_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void spu_register_types(void)
{
    type_register_static(&spu_info);
}

type_init(spu_register_types)
