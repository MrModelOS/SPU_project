/*
 * ppu_qemu.c — QEMU virtual PCI device: Search Processing Unit v0.3
 *
 * BAR0: MMIO registers (4KB) — SPPU + SEU
 * BAR1: Vector memory bank (3MB) — shared between vectors and SEU trees
 * BAR2: DMA buffer for host-guest transfers (4MB)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_regs.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qom/object.h"

/* ---- SPPU register offsets ---- */
#define SPPU_REG_CTRL         0x00
#define SPPU_REG_STATUS       0x04
#define SPPU_REG_VEC_COUNT    0x08
#define SPPU_REG_DIMENSION    0x0C
#define SPPU_REG_TARGET_ADDR  0x10
#define SPPU_REG_RESULT_IDX   0x18
#define SPPU_REG_RESULT_SCORE 0x1C
#define SPPU_REG_DEVICE_ID    0x20
#define SPPU_REG_INT_MASK     0x24
#define SPPU_REG_INT_STATUS   0x28
#define SPPU_REG_MAGIC        0xFC

/* ---- SEU register offsets ---- */
#define SPPU_REG_SEU_CTRL       0x30
#define SPPU_REG_SEU_STATUS     0x34
#define SPPU_REG_SEU_DEPTH      0x38
#define SPPU_REG_SEU_OFFSET     0x3C
#define SPPU_REG_SEU_TREE_ADDR  0x40
#define SPPU_REG_SEU_TREE_RESULT 0x44
#define SPPU_REG_SEU_PROB_BASE  0x48
#define SPPU_REG_SEU_IRQ_STATUS 0x4C
#define SPPU_REG_SEU_PROB_READ_IDX  0x50
#define SPPU_REG_SEU_PROB_READBACK  0x54
#define SPPU_REG_SEU_TREE_ENTRIES_TOTAL 0x58

/* ---- SPPU control flags ---- */
#define SPPU_CTRL_START   (1U << 0)
#define SPPU_CTRL_RESET   (1U << 1)
#define SPPU_CTRL_INT_EN  (1U << 2)

/* ---- SEU control flags ---- */
#define SPPU_SEU_START    (1U << 0)
#define SPPU_SEU_RESET    (1U << 1)
#define SPPU_SEU_IRQ_EN   (1U << 2)

/* ---- Status codes ---- */
#define SPPU_STATUS_READY    0x00
#define SPPU_STATUS_BUSY     0x01
#define SPPU_STATUS_DONE     0x02
#define SPPU_STATUS_ERR_DMA  0x03

#define SPPU_SEU_STATUS_READY 0x00
#define SPPU_SEU_STATUS_BUSY  0x01
#define SPPU_SEU_STATUS_DONE  0x02

/* ---- Interrupt bits ---- */
#define SPPU_INT_COMPLETE  (1U << 0)
#define SPPU_INT_ERR_DMA   (1U << 1)
#define SPPU_INT_SEU_DONE  (1U << 2)

/* ---- PCI IDs ---- */
#define SPPU_PCI_VENDOR_ID  0x1234
#define SPPU_PCI_DEVICE_ID  0x5780
#define SPPU_DEVICE_VERSION 0x0003
#define SPPU_MAGIC_VALUE    0x53505520

/* ---- Memory limits ---- */
#define SPPU_MAX_VECTORS    1000
#define SPPU_MAX_DIMENSION  768
#define SPPU_BAR0_SIZE      0x1000
#define SPPU_VEC_MEM_SIZE   (SPPU_MAX_VECTORS * SPPU_MAX_DIMENSION * (int)sizeof(float))
#define SPPU_DMA_SIZE       (4 * 1024 * 1024)

/* ---- SEU tree limits ---- */
#define SPPU_SEU_VARIANTS   16
#define SPPU_SEU_MAX_DEPTH  8
#define SPPU_SEU_TREE_ENTRIES (SPPU_SEU_VARIANTS * SPPU_SEU_MAX_DEPTH)

#define TYPE_SPPU_PCI_DEVICE "sppu-pci"
OBJECT_DECLARE_SIMPLE_TYPE(SPPUPCIState, SPPU_PCI_DEVICE)

struct SPPUPCIState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion vector_mem;
    MemoryRegion dma_mem;

    /* SPPU registers (up to 0x4C / 4 + 1 = 20 entries) */
    uint32_t regs[32];

    float *vec_bank;
    uint8_t *dma_buf;
    float target[SPPU_MAX_DIMENSION];

    /* SEU tree result buffer */
    float seu_tree[SPPU_SEU_TREE_ENTRIES];
};

/* ---- SPPU search emulation ---- */

static void sppu_run_search(SPPUPCIState *s)
{
    uint32_t count = s->regs[SPPU_REG_VEC_COUNT / 4];
    uint32_t dim   = s->regs[SPPU_REG_DIMENSION / 4];
    if (count > SPPU_MAX_VECTORS) count = SPPU_MAX_VECTORS;
    if (dim > SPPU_MAX_DIMENSION) dim = SPPU_MAX_DIMENSION;

    s->regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_BUSY;

    int best_index = -1;
    float max_score = -1e9f;

    for (uint32_t i = 0; i < count; i++) {
        float dot = 0.0f;
        float *vec = s->vec_bank + (size_t)i * SPPU_MAX_DIMENSION;
        for (uint32_t d = 0; d < dim; d++) {
            dot += vec[d] * s->target[d];
        }
        if (dot > max_score) {
            max_score = dot;
            best_index = (int)i;
        }
    }

    s->regs[SPPU_REG_RESULT_IDX / 4] = (uint32_t)best_index;
    memcpy(&s->regs[SPPU_REG_RESULT_SCORE / 4], &max_score, sizeof(float));
    s->regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_DONE;

    if (s->regs[SPPU_REG_INT_MASK / 4] & SPPU_CTRL_INT_EN) {
        s->regs[SPPU_REG_INT_STATUS / 4] |= SPPU_INT_COMPLETE;
        pci_set_irq(&s->parent_obj, 1);
    }

    printf("[SPPU-QEMU] Search: %u vec dim=%u best=#%d score=%.4f\n",
           count, dim, best_index, max_score);
}

/* ---- SEU tree emulation ---- */

static void sppu_seu_run_tree(SPPUPCIState *s)
{
    uint32_t depth = s->regs[SPPU_REG_SEU_DEPTH / 4];
    uint32_t offset = s->regs[SPPU_REG_SEU_OFFSET / 4];
    if (depth < 5) depth = 5;
    if (depth > 8) depth = 8;

    s->regs[SPPU_REG_SEU_STATUS / 4] = SPPU_SEU_STATUS_BUSY;

    uint32_t seed = 0x00000001;
    uint32_t total = SPPU_SEU_VARIANTS * depth;

    for (uint32_t i = 0; i < total; i++) {
        uint32_t level = i % depth;
        float score;
        uint32_t bits = seed ^ (offset << (level & 3)) + (level << 1);
        memcpy(&score, &bits, sizeof(float));
        s->seu_tree[i] = score;

        /* LFSR shift */
        uint32_t feedback = (seed >> 31) ^ (seed >> 5);
        seed = (seed << 1) | (feedback & 1);
    }

    s->regs[SPPU_REG_SEU_TREE_RESULT / 4] = total;
    s->regs[SPPU_REG_SEU_STATUS / 4] = SPPU_SEU_STATUS_DONE;

    if (s->regs[SPPU_REG_SEU_CTRL / 4] & SPPU_SEU_IRQ_EN) {
        s->regs[SPPU_REG_SEU_IRQ_STATUS / 4] |= SPPU_INT_SEU_DONE;
        s->regs[SPPU_REG_INT_STATUS / 4] |= SPPU_INT_SEU_DONE;
        pci_set_irq(&s->parent_obj, 1);
    }

    printf("[SPPU-QEMU] SEU tree: depth=%u variants=%u entries=%u\n",
           depth, SPPU_SEU_VARIANTS, total);
}

/* ---- MMIO read ---- */

static uint64_t sppu_mmio_read(void *opaque, hwaddr addr, unsigned int size)
{
    SPPUPCIState *s = SPPU_PCI_DEVICE(opaque);
    if (addr + size > SPPU_BAR0_SIZE) return 0;
    if (addr == SPPU_REG_DEVICE_ID) return SPPU_DEVICE_VERSION;
    if (addr == SPPU_REG_MAGIC) return SPPU_MAGIC_VALUE;

    /* SEU probability readback */
    if (addr == SPPU_REG_SEU_PROB_READBACK) {
        uint32_t idx = s->regs[SPPU_REG_SEU_PROB_READ_IDX / 4];
        if (idx < SPPU_SEU_TREE_ENTRIES) {
            uint32_t bits;
            memcpy(&bits, &s->seu_tree[idx], sizeof(float));
            return bits;
        }
        return 0;
    }
    if (addr == SPPU_REG_SEU_TREE_ENTRIES_TOTAL) {
        return s->regs[SPPU_REG_SEU_TREE_RESULT / 4];
    }

    return s->regs[addr / 4];
}

/* ---- MMIO write ---- */

static void sppu_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned int size)
{
    SPPUPCIState *s = SPPU_PCI_DEVICE(opaque);
    if (addr + size > SPPU_BAR0_SIZE) return;

    switch (addr) {
    case SPPU_REG_CTRL:
        if (val & SPPU_CTRL_RESET) {
            memset(s->regs, 0, sizeof(s->regs));
            s->regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_READY;
            printf("[SPPU-QEMU] Reset\n");
        }
        if ((val & SPPU_CTRL_START) &&
            (s->regs[SPPU_REG_STATUS / 4] == SPPU_STATUS_READY ||
             s->regs[SPPU_REG_STATUS / 4] == SPPU_STATUS_DONE)) {
            sppu_run_search(s);
        }
        s->regs[SPPU_REG_CTRL / 4] = (uint32_t)val;
        break;

    case SPPU_REG_INT_STATUS:
        if (val & SPPU_INT_COMPLETE) {
            s->regs[SPPU_REG_INT_STATUS / 4] &= ~SPPU_INT_COMPLETE;
            pci_set_irq(&s->parent_obj, 0);
        }
        if (val & SPPU_INT_SEU_DONE) {
            s->regs[SPPU_REG_INT_STATUS / 4] &= ~SPPU_INT_SEU_DONE;
            s->regs[SPPU_REG_SEU_IRQ_STATUS / 4] &= ~SPPU_INT_SEU_DONE;
        }
        break;

    case SPPU_REG_TARGET_ADDR: {
        uint32_t dim = s->regs[SPPU_REG_DIMENSION / 4];
        if (dim > SPPU_MAX_DIMENSION) dim = SPPU_MAX_DIMENSION;
        uint32_t offset = (uint32_t)val;
        if (offset + dim * sizeof(float) <= SPPU_DMA_SIZE) {
            memcpy(s->target, s->dma_buf + offset, dim * sizeof(float));
        }
        s->regs[SPPU_REG_TARGET_ADDR / 4] = (uint32_t)val;
        break;
    }

    case SPPU_REG_SEU_CTRL:
        if (val & SPPU_SEU_RESET) {
            s->regs[SPPU_REG_SEU_STATUS / 4] = SPPU_SEU_STATUS_READY;
            s->regs[SPPU_REG_SEU_IRQ_STATUS / 4] = 0;
            printf("[SPPU-QEMU] SEU Reset\n");
        }
        if ((val & SPPU_SEU_START) &&
            (s->regs[SPPU_REG_SEU_STATUS / 4] == SPPU_SEU_STATUS_READY ||
             s->regs[SPPU_REG_SEU_STATUS / 4] == SPPU_SEU_STATUS_DONE)) {
            sppu_seu_run_tree(s);
        }
        s->regs[SPPU_REG_SEU_CTRL / 4] = (uint32_t)val;
        break;

    case SPPU_REG_SEU_IRQ_STATUS:
        if (val & SPPU_INT_SEU_DONE) {
            s->regs[SPPU_REG_SEU_IRQ_STATUS / 4] &= ~SPPU_INT_SEU_DONE;
            s->regs[SPPU_REG_INT_STATUS / 4] &= ~SPPU_INT_SEU_DONE;
        }
        break;

    default:
        s->regs[addr / 4] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps sppu_mmio_ops = {
    .read = sppu_mmio_read,
    .write = sppu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps sppu_vec_mem_ops = {
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps sppu_dma_ops = {
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void sppu_realize(PCIDevice *pdev, Error **errp)
{
    SPPUPCIState *s = SPPU_PCI_DEVICE(pdev);

    pci_set_word(pdev->config + PCI_VENDOR_ID, SPPU_PCI_VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, SPPU_PCI_DEVICE_ID);
    pdev->config[PCI_REVISION_ID] = 0x03;
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pdev->config[PCI_INTERRUPT_PIN] = 1;

    s->vec_bank = g_malloc0(SPPU_VEC_MEM_SIZE);
    s->dma_buf  = g_malloc0(SPPU_DMA_SIZE);
    if (!s->vec_bank || !s->dma_buf) {
        error_setg(errp, "sppu: allocation failed");
        return;
    }

    /* BAR0: MMIO registers */
    memory_region_init_io(&s->mmio, OBJECT(s), &sppu_mmio_ops, s,
                          "sppu-mmio", SPPU_BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* BAR1: Vector memory bank */
    memory_region_init_io(&s->vector_mem, OBJECT(s), &sppu_vec_mem_ops, s,
                          "sppu-vec-mem", SPPU_VEC_MEM_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->vector_mem);

    /* BAR2: DMA buffer (host-guest transfers) */
    memory_region_init_io(&s->dma_mem, OBJECT(s), &sppu_dma_ops, s,
                          "sppu-dma", SPPU_DMA_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->dma_mem);

    s->regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_READY;
    s->regs[SPPU_REG_SEU_STATUS / 4] = SPPU_SEU_STATUS_READY;
    printf("[SPPU-QEMU] Ready v0.3: BAR0=%uKB BAR1=%dMB BAR2=%dMB SEU=16x8\n",
           SPPU_BAR0_SIZE / 1024, SPPU_VEC_MEM_SIZE / (1024 * 1024),
           SPPU_DMA_SIZE / (1024 * 1024));
}

static void sppu_unrealize(PCIDevice *pdev)
{
    SPPUPCIState *s = SPPU_PCI_DEVICE(pdev);
    g_free(s->vec_bank);
    g_free(s->dma_buf);
}

static const VMStateDescription sppu_vmstate = {
    .name = TYPE_SPPU_PCI_DEVICE,
    .version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, SPPUPCIState, 32),
        VMSTATE_FLOAT_ARRAY(seu_tree, SPPUPCIState, SPPU_SEU_TREE_ENTRIES),
        VMSTATE_END_OF_LIST()
    }
};

static void sppu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = sppu_realize;
    k->exit = sppu_unrealize;
    k->vendor_id = SPPU_PCI_VENDOR_ID;
    k->device_id = SPPU_PCI_DEVICE_ID;

    dc->desc = "SPPU - Search Processing Unit with SEU";
    dc->vmsd = &sppu_vmstate;
}

static const TypeInfo sppu_info = {
    .name          = TYPE_SPPU_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SPPUPCIState),
    .class_init    = sppu_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void sppu_register_types(void)
{
    type_register_static(&sppu_info);
}

type_init(sppu_register_types)
