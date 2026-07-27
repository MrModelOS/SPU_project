/*
 * SPPU Device Header
 * Карта MMIO-регистров и константы Search Processing Unit
 *
 * Совместимость: C11 userspace / Linux Kernel (__KERNEL__)
 * Выравнивание: все 32-битные регистры выровнены по границе 4 байт.
 */

#ifndef SPPU_DEVICE_H
#define SPPU_DEVICE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/* ==========================================================================
 * PCI Express идентификаторы
 * ========================================================================== */
#define SPPU_PCI_VENDOR_ID	0x1234U
#define SPPU_PCI_DEVICE_ID	0x5781U	/* SPPU Generation 1 (Zynq-7010) */

/* ==========================================================================
 * BAR0: MMIO Register Space
 * ========================================================================== */
#define SPPU_BAR0_SIZE		0x1000U	/* 4 KiB MMIO window (0x000..0xFFF) */

/*
 * Карта регистров (смещения от BASE + 0)
 * Все смещения кратны 4.
 */
#define SPPU_REG_CTRL		0x00U	/* RW: Управление (start/reset/irq_en) */
#define SPPU_REG_STATUS		0x04U	/* RO: Статус устройства               */
#define SPPU_REG_VEC_COUNT	0x08U	/* RW: Количество векторов в банке     */
#define SPPU_REG_DIMENSION	0x0CU	/* RW: Размерность вектора (dim)       */
#define SPPU_REG_TARGET_ADDR	0x10U	/* RW: Базовый адрес target-вектора*   */
#define SPPU_REG_RESULT_IDX	0x18U	/* RO: Индекс наилучшего совпадения    */
#define SPPU_REG_RESULT_SCORE	0x1CU	/* RO: Оценка сходства (raw float)     */
#define SPPU_REG_DEVICE_ID	0x20U	/* RO: ID устройства / версия прошивки */
#define SPPU_REG_INT_MASK	0x24U	/* RW: Маска прерываний                */
#define SPPU_REG_INT_STATUS	0x28U	/* RW1C: Статус прерываний             */

/* ==========================================================================
 * Флаги управления — SPPU_REG_CTRL
 * ========================================================================== */
#define SPPU_CTRL_START		(1U << 0)	/* Запуск поиска (писать 1)   */
#define SPPU_CTRL_RESET		(1U << 1)	/* Сброс конвейера и FIFO     */
#define SPPU_CTRL_INT_EN		(1U << 2)	/* Разрешить прерывание DONE  */

/* ==========================================================================
 * Коды статуса — SPPU_REG_STATUS
 * ========================================================================== */
#define SPPU_STATUS_READY	0x00U
#define SPPU_STATUS_BUSY		0x01U
#define SPPU_STATUS_DONE		0x02U
#define SPPU_STATUS_ERR_DMA	0x03U

/* ==========================================================================
 * SEU (Speculative Execution Unit) — MMIO Registers
 * ========================================================================== */
#define SPPU_REG_SEU_CTRL	0x30U	/* RW: SEU управление (start/reset/tree_cfg) */
#define SPPU_REG_SEU_STATUS	0x34U	/* RO: SEU статус (busy/done/tree_ready)    */
#define SPPU_REG_SEU_DEPTH	0x38U	/* RW: Глубина дерева (5..8)                */
#define SPPU_REG_SEU_OFFSET	0x3CU	/* RW: Смещения веток (packed)              */
#define SPPU_REG_SEU_TREE_ADDR	0x40U	/* RW: Базовый адрес дерева в vmem          */
#define SPPU_REG_SEU_TREE_RESULT	0x44U	/* RO: Адрес результата дерева              */
#define SPPU_REG_SEU_PROB_BASE	0x48U	/* RW: База конфигурации вероятностей       */
#define SPPU_REG_SEU_IRQ_STATUS	0x4CU	/* RW1C: Статус прерываний SEU              */
#define SPPU_REG_SEU_PROB_READ_IDX  0x50U	/* RW: Индекс для чтения вероятности (0..127) */
#define SPPU_REG_SEU_PROB_READBACK  0x54U	/* RO: Значение вероятности по индексу       */
#define SPPU_REG_SEU_TREE_ENTRIES_TOTAL 0x58U	/* RO: Всего записей дерева               */

/* SEU control flags — SPPU_REG_SEU_CTRL */
#define SPPU_SEU_START		(1U << 0)	/* Запуск генерации дерева     */
#define SPPU_SEU_RESET		(1U << 1)	/* Сброс состояния SEU         */
#define SPPU_SEU_IRQ_EN		(1U << 2)	/* Разрешить прерывание DONE  */
#define SPPU_SEU_TREE_16B	(1U << 3)	/* 16-ветвевое дерево         */

/* SEU status codes — SPPU_REG_SEU_STATUS */
#define SPPU_SEU_STATUS_READY	0x00U
#define SPPU_SEU_STATUS_BUSY	0x01U
#define SPPU_SEU_STATUS_DONE	0x02U
#define SPPU_SEU_STATUS_ERR	0x03U

/* SEU depth limits */
#ifndef SPPU_SEU_MIN_DEPTH
#define SPPU_SEU_MIN_DEPTH	5U
#endif
#ifndef SPPU_SEU_MAX_DEPTH
#define SPPU_SEU_MAX_DEPTH	8U
#endif
#ifndef SPPU_SEU_VARIANTS
#define SPPU_SEU_VARIANTS	16U	/* Количество вариантов продолжения */
#endif

/* SEU tree: 16 branches x max 8 levels = 128 entries (probability scores) */
#ifndef SPPU_SEU_TREE_ENTRIES
#define SPPU_SEU_TREE_ENTRIES	(SPPU_SEU_VARIANTS * SPPU_SEU_MAX_DEPTH)
#endif

/* ==========================================================================
 * SEU v0.4 — Speculative Tree Walker Registers
 * ========================================================================== */
#define SPPU_REG_SEU_TREE_CONFIG		0x60U	/* RW: Tree config (branches/context/auto_validate) */
#define SPPU_REG_SEU_NODE_BASE_ADDR	0x64U	/* RW: Base address for tree node data in vmem   */
#define SPPU_REG_SEU_BRANCH_MASK		0x68U	/* RW: 16-bit mask of active branches             */
#define SPPU_REG_SEU_TREE_RESULT_FLAGS	0x6CU	/* RO: Per-branch valid/done flags                */
#define SPPU_REG_SEU_CONTEXT_ADDR	0x70U	/* RW: Address of input context tokens in vmem    */
#define SPPU_REG_SEU_EMBED_ADDR		0x74U	/* RW: Address for embedding search target        */
#define SPPU_REG_SEU_BRANCH_BEST_IDX	0x78U	/* RO: Best branch index                          */
#define SPPU_REG_SEU_BRANCH_BEST_SCORE	0x7CU	/* RO: Best branch score (raw float)              */

/* Tree config field encodings — SPPU_REG_SEU_TREE_CONFIG */
#define SPPU_TREE_CFG_MAX_BRANCHES_SHIFT	0U
#define SPPU_TREE_CFG_MAX_BRANCHES_MASK	0xFU		/* bits [3:0]: 1..16 */
#define SPPU_TREE_CFG_CTX_LEN_SHIFT	4U
#define SPPU_TREE_CFG_CTX_LEN_MASK	0xFU		/* bits [7:4]: 1..8  */
#define SPPU_TREE_CFG_AUTO_VALIDATE	(1U << 8)	/* bit 8: auto-validate via embed search */

/* Tree status extension — returned in tree_status[31:16] */
#define SPPU_TREE_STATUS_EMBED_DONE	BIT(16)		/* embed search completed */
#define SPPU_TREE_STATUS_VALIDATED	BIT(17)		/* branches validated */
#define SPPU_TREE_STATUS_ROLLED_BACK	BIT(18)		/* rollback occurred */

/* ==========================================================================
 * Speculative tree context ioctl structures
 * ========================================================================== */
struct sppu_seu_tree_context {
	uint32_t context_addr;		/* Address of context tokens in vmem      */
	uint32_t embed_addr;		/* Address of embedding target in vmem    */
	uint32_t tree_config;		/* Packed config (branches/ctx/validate)  */
	uint32_t node_base_addr;	/* Base address for node weight data      */
	uint16_t branch_mask;		/* Which branches to explore (16-bit)     */
	uint16_t _pad;
};

struct sppu_seu_branch_result {
	uint32_t branch_valid;		/* 16-bit mask of completed branches      */
	uint32_t best_idx;		/* Index of best-scoring branch           */
	uint32_t best_score;		/* Score of best branch (raw float)       */
	uint32_t tree_entries;		/* Total tree entries written             */
};

/* ==========================================================================
 * Прерывания — SPPU_REG_INT_STATUS / SPPU_REG_INT_MASK
 * ========================================================================== */
#define SPPU_INT_COMPLETE	(1U << 0)
#define SPPU_INT_ERR_DMA		(1U << 1)
#define SPPU_INT_SEU_DONE	(1U << 2)

/* ==========================================================================
 * Лимиты памяти и данных
 * XC7Z010: 4K x 32-bit vmem (16KB BRAM, 8 BRAM18K blocks)
 * ========================================================================== */
#define SPPU_MAX_VECTORS	25U	/* max vectors in 4K vmem (at dim=128) */
#define SPPU_MAX_DIMENSION	768U	/* max dimension (software limit) */
#define SPPU_MAX_DIM_BYTES	(SPPU_MAX_DIMENSION * sizeof(float))
#define SPPU_VMEM_DEPTH		4096U	/* XC7Z010 vmem: 4K entries */

/* ==========================================================================
 * Вспомогательные макросы
 * ========================================================================== */
#define SPPU_REG_IDX(off)	((off) / sizeof(uint32_t))
#define SPPU_REG_ALIGNED(off)	(((off) & 0x3U) == 0U)

/* ==========================================================================
 * Userspace <-> Kernel ioctl interface
 * ========================================================================== */
#ifndef __KERNEL__
#include <sys/ioctl.h>
#else
#include <linux/ioctl.h>
#endif

struct sppu_ioctl_param {
	uint32_t vec_count;
	uint32_t dimension;
};

struct sppu_seu_config {
	uint32_t depth;		/* Глубина дерева (5..8) */
	uint32_t offset;	/* Смещения веток       */
	uint32_t tree_addr;	/* Базовый адрес в vmem */
	uint32_t prob_base;	/* База вероятностей    */
};

struct sppu_seu_tree_result {
	uint32_t entries[SPPU_SEU_TREE_ENTRIES]; /* 128 x probability scores */
	uint32_t status;
};

struct sppu_ioctl_vector {
	uint32_t index;
	uint32_t dim;
	float data[SPPU_MAX_DIMENSION];
};

struct sppu_ioctl_result {
	uint32_t index;
	float score;
	uint32_t status;
};

#define SPPU_IOCTL_MAGIC		'S'
#define SPPU_IOCTL_RESET		_IO(SPPU_IOCTL_MAGIC, 0)
#define SPPU_IOCTL_SET_PARAM	_IOW(SPPU_IOCTL_MAGIC, 1, struct sppu_ioctl_param)
#define SPPU_IOCTL_LOAD_VEC	_IOW(SPPU_IOCTL_MAGIC, 2, struct sppu_ioctl_vector)
#define SPPU_IOCTL_SET_TARGET	_IOW(SPPU_IOCTL_MAGIC, 3, struct sppu_ioctl_vector)
#define SPPU_IOCTL_START		_IO(SPPU_IOCTL_MAGIC, 4)
#define SPPU_IOCTL_GET_RESULT	_IOR(SPPU_IOCTL_MAGIC, 5, struct sppu_ioctl_result)
#define SPPU_IOCTL_GET_STATUS	_IOR(SPPU_IOCTL_MAGIC, 6, uint32_t)

/* SEU ioctls */
#define SPPU_IOCTL_SEU_CONFIG	_IOW(SPPU_IOCTL_MAGIC, 7, struct sppu_seu_config)
#define SPPU_IOCTL_SEU_START	_IO(SPPU_IOCTL_MAGIC, 8)
#define SPPU_IOCTL_SEU_GET_TREE	_IOR(SPPU_IOCTL_MAGIC, 9, struct sppu_seu_tree_result)

/* SEU v0.4 — speculative tree walker ioctls */
#define SPPU_IOCTL_SEU_BUILD_TREE	_IOW(SPPU_IOCTL_MAGIC, 10, struct sppu_seu_tree_context)
#define SPPU_IOCTL_SEU_GET_BRANCHES	_IOR(SPPU_IOCTL_MAGIC, 11, struct sppu_seu_branch_result)
#define SPPU_IOCTL_SEU_ACCEPT		_IO(SPPU_IOCTL_MAGIC, 12)
#define SPPU_IOCTL_SEU_ROLLBACK		_IO(SPPU_IOCTL_MAGIC, 13)

#endif /* SPPU_DEVICE_H */
