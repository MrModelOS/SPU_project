/*
 * SPPU (Search Processing Unit) Kernel Driver v0.2
 *
 * Dual-mode: software emulation + real PCI hardware.
 * Features:
 *   - PCI probe/remove with BAR0 MMIO + BAR2 DMA mapping
 *   - Char device /dev/sppu with ioctl interface
 *   - IRQ-driven completion (no polling needed)
 *   - DMA engine for hardware vector loading
 *   - Software emulation mode (emulation=1)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/ioctl.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <asm/fpu/api.h>

#include "../emulator/sppu_device.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SPPU Project");
MODULE_DESCRIPTION("SPPU PCI Driver with DMA, IRQ, and emulation support");
MODULE_VERSION("0.3");

static bool emulation = true;
module_param(emulation, bool, 0444);
MODULE_PARM_DESC(emulation, "Enable software emulation (no PCI hardware required)");

#define SPPU_DEV_NAME   "sppu"
#define SPPU_CLASS_NAME "sppu"
#define SPPU_DMA_BAR    2
#define SPPU_DMA_SIZE   (4 * 1024 * 1024)

	struct sppu_dev {
	struct pci_dev *pdev;
	void __iomem *bar0;
	void __iomem *bar2;
	struct cdev cdev;
	struct device *dev;
	dev_t devt;
	struct class *class;
	spinlock_t lock;
	int irq;
	wait_queue_head_t wait_q;
	int done_flag;
	uint32_t *emu_regs;
	float (*emu_mem)[SPPU_MAX_DIMENSION];
	float *emu_target;
	/* SEU emulation state */
	int seu_done_flag;
	float seu_tree[SPPU_SEU_VARIANTS * SPPU_SEU_MAX_DEPTH];
	/* SEU v0.4 — speculative tree walker emulation */
	uint32_t emu_tree_config;
	uint32_t emu_node_base_addr;
	uint16_t emu_branch_mask;
	uint32_t emu_context_addr;
	uint32_t emu_embed_addr;
	uint32_t emu_branch_best_idx;
	uint32_t emu_branch_best_score;
	uint16_t emu_branch_valid;
	float emu_branch_scores[SPPU_SEU_VARIANTS];
};

static struct sppu_dev sppu;

/* Forward declarations */
static noinline void sppu_emu_run_search(struct sppu_dev *s);
static noinline void sppu_emu_run_seu_tree(struct sppu_dev *s);

/* ---- MMIO accessors ---- */

static inline uint32_t sppu_read32(struct sppu_dev *s, uint32_t off)
{
	if (emulation)
		return s->emu_regs[SPPU_REG_IDX(off)];
	return readl(s->bar0 + off);
}

static inline void sppu_write32(struct sppu_dev *s, uint32_t val, uint32_t off)
{
	if (emulation) {
		s->emu_regs[SPPU_REG_IDX(off)] = val;
		if (off == SPPU_REG_CTRL && (val & SPPU_CTRL_START))
			sppu_emu_run_search(s);
		if (off == SPPU_REG_SEU_CTRL && (val & SPPU_SEU_START))
			sppu_emu_run_seu_tree(s);
	} else {
		writel(val, s->bar0 + off);
	}
}

/* ---- Emulation core (dot-product in kernel FPU) ---- */

__attribute__((target("sse")))
static noinline void sppu_emu_run_search(struct sppu_dev *s)
{
	uint32_t count, dim;
	int best_index = -1;
	float max_score = -1.0e9f;
	uint32_t i, d;
	float dot;

	s->emu_regs[SPPU_REG_IDX(SPPU_REG_STATUS)] = SPPU_STATUS_BUSY;

	count = s->emu_regs[SPPU_REG_IDX(SPPU_REG_VEC_COUNT)];
	dim   = s->emu_regs[SPPU_REG_IDX(SPPU_REG_DIMENSION)];

	kernel_fpu_begin();
	for (i = 0; i < count; i++) {
		dot = 0.0f;
		for (d = 0; d < dim; d++)
			dot += s->emu_mem[i][d] * s->emu_target[d];
		if (dot > max_score) {
			max_score = dot;
			best_index = i;
		}
	}
	kernel_fpu_end();

	s->emu_regs[SPPU_REG_IDX(SPPU_REG_RESULT_IDX)] = (uint32_t)best_index;
	memcpy(&s->emu_regs[SPPU_REG_IDX(SPPU_REG_RESULT_SCORE)],
	       &max_score, sizeof(float));
	s->emu_regs[SPPU_REG_IDX(SPPU_REG_STATUS)] = SPPU_STATUS_DONE;
}

/* ---- SEU tree emulation (kernel FPU) ---- */

__attribute__((target("sse")))
static noinline void sppu_emu_run_seu_tree(struct sppu_dev *s)
{
	uint32_t depth, offset, seed, total, i, level;
	uint32_t *regs = s->emu_regs;

	regs[SPPU_REG_IDX(SPPU_REG_SEU_STATUS)] = SPPU_SEU_STATUS_BUSY;

	depth = regs[SPPU_REG_IDX(SPPU_REG_SEU_DEPTH)];
	offset = regs[SPPU_REG_IDX(SPPU_REG_SEU_OFFSET)];
	if (depth < SPPU_SEU_MIN_DEPTH) depth = SPPU_SEU_MIN_DEPTH;
	if (depth > SPPU_SEU_MAX_DEPTH) depth = SPPU_SEU_MAX_DEPTH;

	total = SPPU_SEU_VARIANTS * depth;
	seed = 0x00000001;

	kernel_fpu_begin();
	for (i = 0; i < total; i++) {
		level = i % depth;
		uint32_t bits = seed ^ (offset << (level & 3)) + (level << 1);
		memcpy(&s->seu_tree[i], &bits, sizeof(float));

		/* LFSR shift */
		uint32_t feedback = (seed >> 31) ^ (seed >> 5);
		seed = (seed << 1) | (feedback & 1);
	}
	kernel_fpu_end();

	regs[SPPU_REG_IDX(SPPU_REG_SEU_TREE_RESULT)] = total;
	regs[SPPU_REG_IDX(SPPU_REG_SEU_STATUS)] = SPPU_SEU_STATUS_DONE;

	if (regs[SPPU_REG_IDX(SPPU_REG_SEU_CTRL)] & SPPU_SEU_IRQ_EN) {
		regs[SPPU_REG_IDX(SPPU_REG_SEU_IRQ_STATUS)] |= SPPU_INT_SEU_DONE;
		regs[SPPU_REG_IDX(SPPU_REG_INT_STATUS)] |= SPPU_INT_SEU_DONE;
	}

	s->seu_done_flag = 1;
	wake_up_interruptible(&s->wait_q);

	pr_info("sppu: SEU tree done, depth=%u entries=%u\n", depth, total);
}

/* ---- SEU v0.4 — Speculative tree build emulation (kernel FPU) ---- */

__attribute__((target("sse")))
static noinline void sppu_emu_build_tree(struct sppu_dev *s)
{
	uint32_t *regs = s->emu_regs;
	uint32_t max_branches, ctx_len, auto_validate;
	uint32_t depth, offset, total;
	uint32_t seed, i, level, variant;
	int best_idx = -1;
	float best_score = -1.0e9f;

	regs[SPPU_REG_IDX(SPPU_REG_SEU_STATUS)] = SPPU_SEU_STATUS_BUSY;

	depth = regs[SPPU_REG_IDX(SPPU_REG_SEU_DEPTH)];
	offset = regs[SPPU_REG_IDX(SPPU_REG_SEU_OFFSET)];
	if (depth < SPPU_SEU_MIN_DEPTH) depth = SPPU_SEU_MIN_DEPTH;
	if (depth > SPPU_SEU_MAX_DEPTH) depth = SPPU_SEU_MAX_DEPTH;

	/* Parse tree config */
	uint32_t cfg = regs[SPPU_REG_IDX(SPPU_REG_SEU_TREE_CONFIG)];
	max_branches = cfg & SPPU_TREE_CFG_MAX_BRANCHES_MASK;
	if (max_branches == 0) max_branches = SPPU_SEU_VARIANTS;
	if (max_branches > SPPU_SEU_VARIANTS) max_branches = SPPU_SEU_VARIANTS;
	ctx_len = (cfg >> SPPU_TREE_CFG_CTX_LEN_SHIFT) & SPPU_TREE_CFG_CTX_LEN_MASK;
	if (ctx_len == 0) ctx_len = 1;
	auto_validate = cfg & SPPU_TREE_CFG_AUTO_VALIDATE;

	uint16_t branch_mask = (uint16_t)(regs[SPPU_REG_IDX(SPPU_REG_SEU_BRANCH_MASK)] & 0xFFFF);

	total = max_branches * depth;
	seed = 0x00000001;

	kernel_fpu_begin();
	for (i = 0; i < total; i++) {
		level = i % depth;
		variant = i / depth;

		/* Weight-based scoring: combine context with LFSR */
		uint32_t bits = seed ^ (offset << (level & 3)) + (level << 1);
		memcpy(&s->seu_tree[i], &bits, sizeof(float));

		/* Track per-branch scores for accept/rollback */
		float score = (float)(bits & 0x0000FFFF) / 65535.0f;
		s->emu_branch_scores[variant] += score;

		/* LFSR shift */
		uint32_t feedback = (seed >> 31) ^ (seed >> 5);
		seed = (seed << 1) | (feedback & 1);

		/* Find best branch */
		if (score > best_score) {
			best_score = score;
			best_idx = (int)variant;
		}
	}

	/* Compute branch validity from mask */
	s->emu_branch_valid = branch_mask & ((1U << max_branches) - 1);
	s->emu_branch_best_idx = (uint32_t)(best_idx >= 0 ? best_idx : 0);
	s->emu_branch_best_score = 0;
	if (best_idx >= 0) {
		memcpy(&s->emu_branch_best_score,
		       &s->emu_branch_scores[best_idx], sizeof(float));
	}
	kernel_fpu_end();

	regs[SPPU_REG_IDX(SPPU_REG_SEU_TREE_RESULT)] = total;
	regs[SPPU_REG_IDX(SPPU_REG_SEU_STATUS)] = SPPU_SEU_STATUS_DONE;

	if (regs[SPPU_REG_IDX(SPPU_REG_SEU_CTRL)] & SPPU_SEU_IRQ_EN) {
		regs[SPPU_REG_IDX(SPPU_REG_SEU_IRQ_STATUS)] |= SPPU_INT_SEU_DONE;
		regs[SPPU_REG_IDX(SPPU_REG_INT_STATUS)] |= SPPU_INT_SEU_DONE;
	}

	s->seu_done_flag = 1;
	wake_up_interruptible(&s->wait_q);

	pr_info("sppu: SEU tree v0.4 built, branches=%u depth=%u entries=%u mask=0x%04x\n",
		max_branches, depth, total, branch_mask);
}

/* ---- Hardware-mode helpers ---- */

static void sppu_hw_start_search(struct sppu_dev *s)
{
	sppu_write32(s, SPPU_CTRL_INT_EN | SPPU_CTRL_START, SPPU_REG_CTRL);
}

static int sppu_hw_load_vector(struct sppu_dev *s, uint32_t index,
			      const float *data, uint32_t dim)
{
	size_t offset, bytes;

	if (!s->bar2)
		return -ENODEV;

	offset = SPPU_MAX_DIMENSION * sizeof(float) +
		 (size_t)index * SPPU_MAX_DIMENSION * sizeof(float);
	bytes  = dim * sizeof(float);

	if (offset + bytes > SPPU_DMA_SIZE)
		return -EINVAL;

	memcpy_toio(s->bar2 + offset, data, bytes);
	return 0;
}

static int sppu_hw_set_target(struct sppu_dev *s, const float *data, uint32_t dim)
{
	size_t bytes = dim * sizeof(float);

	if (!s->bar2)
		return -ENODEV;
	if (bytes > SPPU_DMA_SIZE)
		return -EINVAL;

	memcpy_toio(s->bar2, data, bytes);
	return 0;
}

static void sppu_hw_get_result(struct sppu_dev *s, struct sppu_ioctl_result *r)
{
	uint32_t raw;

	r->index  = sppu_read32(s, SPPU_REG_RESULT_IDX);
	r->status = sppu_read32(s, SPPU_REG_STATUS);
	raw = sppu_read32(s, SPPU_REG_RESULT_SCORE);
	memcpy(&r->score, &raw, sizeof(float));
}

/* ---- IRQ handler ---- */

static irqreturn_t sppu_irq_handler(int irq, void *dev_id)
{
	struct sppu_dev *s = dev_id;
	uint32_t int_status;

	if (emulation)
		return IRQ_NONE;

	int_status = sppu_read32(s, SPPU_REG_INT_STATUS);
	if (!(int_status & (SPPU_INT_COMPLETE | SPPU_INT_SEU_DONE)))
		return IRQ_NONE;

	if (int_status & SPPU_INT_COMPLETE) {
		sppu_write32(s, SPPU_INT_COMPLETE, SPPU_REG_INT_STATUS);
		s->done_flag = 1;
	}
	if (int_status & SPPU_INT_SEU_DONE) {
		sppu_write32(s, SPPU_INT_SEU_DONE, SPPU_REG_INT_STATUS);
		s->seu_done_flag = 1;
	}
	wake_up_interruptible(&s->wait_q);
	return IRQ_HANDLED;
}

static int sppu_hw_wait_done(struct sppu_dev *s, unsigned int timeout_ms)
{
	int ret;

	if (s->done_flag) {
		s->done_flag = 0;
		return 0;
	}

	ret = wait_event_interruptible_timeout(s->wait_q, s->done_flag,
					       msecs_to_jiffies(timeout_ms));
	if (ret > 0) {
		s->done_flag = 0;
		return 0;
	}
	return -ETIMEDOUT;
}

/* ---- Chardev file operations ---- */

static int sppu_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &sppu;
	sppu.done_flag = 0;
	return 0;
}

static int sppu_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long sppu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct sppu_dev *s = filp->private_data;
	struct sppu_ioctl_param param;
	struct sppu_ioctl_vector *vec = NULL;
	struct sppu_ioctl_result result;
	struct sppu_seu_config seu_cfg;
	struct sppu_seu_tree_result seu_tree_result;
	unsigned long flags;
	int ret = 0;
	uint32_t status = 0;
	bool need_result = false;
	bool need_status = false;
	bool need_seu_tree = false;

	switch (cmd) {
	case SPPU_IOCTL_RESET:
	case SPPU_IOCTL_START:
		break;
	case SPPU_IOCTL_SET_PARAM:
		if (copy_from_user(&param, (void __user *)arg, sizeof(param)))
			return -EFAULT;
		if (param.dimension > SPPU_MAX_DIMENSION)
			return -EINVAL;
		break;
	case SPPU_IOCTL_LOAD_VEC:
	case SPPU_IOCTL_SET_TARGET:
		vec = kmalloc(sizeof(*vec), GFP_KERNEL);
		if (!vec)
			return -ENOMEM;
		if (copy_from_user(vec, (void __user *)arg, sizeof(*vec))) {
			kfree(vec);
			return -EFAULT;
		}
		if (cmd == SPPU_IOCTL_LOAD_VEC) {
			if (vec->index >= SPPU_MAX_VECTORS ||
			    vec->dim > SPPU_MAX_DIMENSION) {
				kfree(vec);
				return -EINVAL;
			}
		} else if (vec->dim > SPPU_MAX_DIMENSION) {
			kfree(vec);
			return -EINVAL;
		}
		break;
	case SPPU_IOCTL_GET_RESULT:
		need_result = true;
		break;
	case SPPU_IOCTL_GET_STATUS:
		need_status = true;
		break;
	case SPPU_IOCTL_SEU_CONFIG:
		if (copy_from_user(&seu_cfg, (void __user *)arg, sizeof(seu_cfg)))
			return -EFAULT;
		if (seu_cfg.depth < SPPU_SEU_MIN_DEPTH || seu_cfg.depth > SPPU_SEU_MAX_DEPTH)
			return -EINVAL;
		break;
	case SPPU_IOCTL_SEU_START:
		break;
	case SPPU_IOCTL_SEU_GET_TREE:
		need_seu_tree = true;
		break;
	case SPPU_IOCTL_SEU_BUILD_TREE: {
		struct sppu_seu_tree_context tree_ctx;
		if (copy_from_user(&tree_ctx, (void __user *)arg, sizeof(tree_ctx)))
			return -EFAULT;
		break;
	}
	case SPPU_IOCTL_SEU_GET_BRANCHES:
		/* Handled inline in the action switch below */
		break;
	case SPPU_IOCTL_SEU_ACCEPT:
	case SPPU_IOCTL_SEU_ROLLBACK:
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&s->lock, flags);

	switch (cmd) {
	case SPPU_IOCTL_RESET:
		sppu_write32(s, SPPU_CTRL_RESET, SPPU_REG_CTRL);
		sppu_write32(s, 0, SPPU_REG_CTRL);
		s->done_flag = 0;
		break;
	case SPPU_IOCTL_SET_PARAM:
		sppu_write32(s, param.vec_count, SPPU_REG_VEC_COUNT);
		sppu_write32(s, param.dimension, SPPU_REG_DIMENSION);
		break;
	case SPPU_IOCTL_LOAD_VEC:
		if (emulation) {
			memcpy(s->emu_mem[vec->index], vec->data,
			       vec->dim * sizeof(float));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			ret = sppu_hw_load_vector(s, vec->index,
						  vec->data, vec->dim);
			spin_lock_irqsave(&s->lock, flags);
		}
		break;
	case SPPU_IOCTL_SET_TARGET:
		if (emulation) {
			memcpy(s->emu_target, vec->data,
			       vec->dim * sizeof(float));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			ret = sppu_hw_set_target(s, vec->data, vec->dim);
			spin_lock_irqsave(&s->lock, flags);
		}
		break;
	case SPPU_IOCTL_START:
		if (emulation) {
			sppu_write32(s, SPPU_CTRL_START, SPPU_REG_CTRL);
		} else {
			sppu_write32(s, SPPU_CTRL_INT_EN, SPPU_REG_INT_MASK);
			sppu_hw_start_search(s);
		}
		break;
	case SPPU_IOCTL_GET_RESULT:
		if (emulation) {
			result.index = sppu_read32(s, SPPU_REG_RESULT_IDX);
			result.status = sppu_read32(s, SPPU_REG_STATUS);
			memcpy(&result.score,
			       &s->emu_regs[SPPU_REG_IDX(SPPU_REG_RESULT_SCORE)],
			       sizeof(float));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			sppu_hw_get_result(s, &result);
			spin_lock_irqsave(&s->lock, flags);
		}
		break;
	case SPPU_IOCTL_GET_STATUS:
		status = sppu_read32(s, SPPU_REG_STATUS);
		break;
	case SPPU_IOCTL_SEU_CONFIG:
		sppu_write32(s, seu_cfg.depth, SPPU_REG_SEU_DEPTH);
		sppu_write32(s, seu_cfg.offset, SPPU_REG_SEU_OFFSET);
		sppu_write32(s, seu_cfg.tree_addr, SPPU_REG_SEU_TREE_ADDR);
		sppu_write32(s, seu_cfg.prob_base, SPPU_REG_SEU_PROB_BASE);
		break;
	case SPPU_IOCTL_SEU_START:
		if (emulation) {
			sppu_write32(s, SPPU_SEU_START, SPPU_REG_SEU_CTRL);
		} else {
			sppu_write32(s, SPPU_SEU_IRQ_EN, SPPU_REG_SEU_CTRL);
			sppu_write32(s, SPPU_SEU_IRQ_EN | SPPU_SEU_START,
				    SPPU_REG_SEU_CTRL);
		}
		break;
	case SPPU_IOCTL_SEU_GET_TREE:
		if (emulation) {
			seu_tree_result.status =
				sppu_read32(s, SPPU_REG_SEU_STATUS);
			memcpy(seu_tree_result.entries, s->seu_tree,
			       sizeof(seu_tree_result.entries));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			seu_tree_result.status =
				sppu_read32(s, SPPU_REG_SEU_STATUS);
			for (int i = 0; i < SPPU_SEU_TREE_ENTRIES; i++) {
				uint32_t raw = readl(s->bar0 +
					SPPU_REG_SEU_TREE_RESULT);
				memcpy(&seu_tree_result.entries[i], &raw,
				       sizeof(float));
			}
			spin_lock_irqsave(&s->lock, flags);
		}
		break;

	case SPPU_IOCTL_SEU_BUILD_TREE: {
		struct sppu_seu_tree_context tree_ctx;
		if (copy_from_user(&tree_ctx, (void __user *)arg, sizeof(tree_ctx)))
			return -EFAULT;

		if (emulation) {
			s->emu_tree_config    = tree_ctx.tree_config;
			s->emu_node_base_addr = tree_ctx.node_base_addr;
			s->emu_branch_mask    = tree_ctx.branch_mask;
			s->emu_context_addr   = tree_ctx.context_addr;
			s->emu_embed_addr     = tree_ctx.embed_addr;

			sppu_write32(s, tree_ctx.tree_config, SPPU_REG_SEU_TREE_CONFIG);
			sppu_write32(s, tree_ctx.node_base_addr, SPPU_REG_SEU_NODE_BASE_ADDR);
			sppu_write32(s, (uint32_t)tree_ctx.branch_mask, SPPU_REG_SEU_BRANCH_MASK);
			sppu_write32(s, tree_ctx.context_addr, SPPU_REG_SEU_CONTEXT_ADDR);
			sppu_write32(s, tree_ctx.embed_addr, SPPU_REG_SEU_EMBED_ADDR);

			sppu_emu_build_tree(s);
		} else {
			writel(tree_ctx.tree_config, s->bar0 + SPPU_REG_SEU_TREE_CONFIG);
			writel(tree_ctx.node_base_addr, s->bar0 + SPPU_REG_SEU_NODE_BASE_ADDR);
			writel((uint32_t)tree_ctx.branch_mask, s->bar0 + SPPU_REG_SEU_BRANCH_MASK);
			writel(tree_ctx.context_addr, s->bar0 + SPPU_REG_SEU_CONTEXT_ADDR);
			writel(tree_ctx.embed_addr, s->bar0 + SPPU_REG_SEU_EMBED_ADDR);

			sppu_write32(s, SPPU_SEU_IRQ_EN | SPPU_SEU_START, SPPU_REG_SEU_CTRL);
		}
		break;
	}

	case SPPU_IOCTL_SEU_GET_BRANCHES: {
		struct sppu_seu_branch_result branch_result;

		if (emulation) {
			branch_result.branch_valid = (uint32_t)s->emu_branch_valid;
			branch_result.best_idx    = s->emu_branch_best_idx;
			branch_result.best_score  = s->emu_branch_best_score;
			branch_result.tree_entries = sppu_read32(s, SPPU_REG_SEU_TREE_ENTRIES_TOTAL);

			if (copy_to_user((void __user *)arg, &branch_result,
					 sizeof(branch_result)))
				return -EFAULT;
		} else {
			branch_result.branch_valid = readl(s->bar0 + SPPU_REG_SEU_TREE_RESULT_FLAGS);
			branch_result.best_idx    = readl(s->bar0 + SPPU_REG_SEU_BRANCH_BEST_IDX);
			branch_result.best_score  = readl(s->bar0 + SPPU_REG_SEU_BRANCH_BEST_SCORE);
			branch_result.tree_entries = readl(s->bar0 + SPPU_REG_SEU_TREE_ENTRIES_TOTAL);

			if (copy_to_user((void __user *)arg, &branch_result,
					 sizeof(branch_result)))
				return -EFAULT;
		}
		break;
	}

	case SPPU_IOCTL_SEU_ACCEPT:
		/* Mark current branch tree as accepted — no rollback needed */
		if (emulation) {
			s->emu_regs[SPPU_REG_IDX(SPPU_REG_SEU_TREE_CONFIG)] |=
				SPPU_TREE_CFG_AUTO_VALIDATE;
		} else {
			uint32_t cfg = readl(s->bar0 + SPPU_REG_SEU_TREE_CONFIG);
			writel(cfg | SPPU_TREE_CFG_AUTO_VALIDATE,
			       s->bar0 + SPPU_REG_SEU_TREE_CONFIG);
		}
		break;

	case SPPU_IOCTL_SEU_ROLLBACK:
		/* Rollback — reset SEU state and invalidate branches */
		if (emulation) {
			s->emu_branch_valid = 0;
			s->emu_branch_best_idx = 0;
			s->emu_branch_best_score = 0;
			s->emu_regs[SPPU_REG_IDX(SPPU_REG_SEU_STATUS)] =
				SPPU_SEU_STATUS_READY;
		} else {
			sppu_write32(s, SPPU_SEU_RESET, SPPU_REG_SEU_CTRL);
			sppu_write32(s, 0, SPPU_REG_SEU_CTRL);
		}
		break;
	}

	spin_unlock_irqrestore(&s->lock, flags);
	kfree(vec);

	if (ret)
		return ret;
	if (need_result &&
	    copy_to_user((void __user *)arg, &result, sizeof(result)))
		return -EFAULT;
	if (need_status && put_user(status, (uint32_t __user *)arg))
		return -EFAULT;
	if (need_seu_tree &&
	    copy_to_user((void __user *)arg, &seu_tree_result,
			 sizeof(seu_tree_result)))
		return -EFAULT;

	return 0;
}

static const struct file_operations sppu_fops = {
	.owner          = THIS_MODULE,
	.open           = sppu_open,
	.release        = sppu_release,
	.unlocked_ioctl = sppu_ioctl,
	.compat_ioctl   = sppu_ioctl,
};

/* ---- Chardev boilerplate ---- */

static char *sppu_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int sppu_create_chardev(void)
{
	int ret;
	struct class *cls;

	ret = alloc_chrdev_region(&sppu.devt, 0, 1, SPPU_DEV_NAME);
	if (ret) {
		pr_err("sppu: alloc_chrdev_region failed\n");
		return ret;
	}

	cdev_init(&sppu.cdev, &sppu_fops);
	ret = cdev_add(&sppu.cdev, sppu.devt, 1);
	if (ret) {
		pr_err("sppu: cdev_add failed\n");
		goto err_unregister;
	}

	cls = class_create(SPPU_CLASS_NAME);
	if (IS_ERR(cls)) {
		ret = PTR_ERR(cls);
		goto err_cdev;
	}
	cls->devnode = sppu_devnode;
	sppu.class = cls;

	sppu.dev = device_create(sppu.class, NULL, sppu.devt, NULL, SPPU_DEV_NAME);
	if (IS_ERR(sppu.dev)) {
		ret = PTR_ERR(sppu.dev);
		goto err_class;
	}
	return 0;

err_class:
	class_destroy(sppu.class);
err_cdev:
	cdev_del(&sppu.cdev);
err_unregister:
	unregister_chrdev_region(sppu.devt, 1);
	return ret;
}

static void sppu_destroy_chardev(void)
{
	if (sppu.dev)
		device_destroy(sppu.class, sppu.devt);
	if (sppu.class)
		class_destroy(sppu.class);
	cdev_del(&sppu.cdev);
	unregister_chrdev_region(sppu.devt, 1);
}

/* ---- PCI probe / remove ---- */

static int sppu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	resource_size_t bar_start, bar_len;

	pr_info("sppu: probing PCI %04x:%04x\n", pdev->vendor, id->device);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		return ret;
	}

	ret = pci_request_regions(pdev, SPPU_DEV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto err_disable;
	}

	/* BAR0: MMIO registers */
	bar_start = pci_resource_start(pdev, 0);
	bar_len = pci_resource_len(pdev, 0);
	if (bar_len < SPPU_BAR0_SIZE) {
		dev_err(&pdev->dev, "BAR0 too small (%pa)\n", &bar_len);
		ret = -ENODEV;
		goto err_regions;
	}

	sppu.bar0 = pci_iomap(pdev, 0, SPPU_BAR0_SIZE);
	if (!sppu.bar0) {
		dev_err(&pdev->dev, "pci_iomap BAR0 failed\n");
		ret = -ENOMEM;
		goto err_regions;
	}

	/* BAR2: DMA buffer for vector data */
	bar_start = pci_resource_start(pdev, SPPU_DMA_BAR);
	bar_len = pci_resource_len(pdev, SPPU_DMA_BAR);
	if (bar_len >= SPPU_DMA_SIZE) {
		sppu.bar2 = pci_iomap(pdev, SPPU_DMA_BAR, SPPU_DMA_SIZE);
		if (sppu.bar2)
			pr_info("sppu: BAR2 DMA mapped %pa (%llu bytes)\n",
				&bar_start, (unsigned long long)bar_len);
	}

	if (!sppu.bar2)
		pr_warn("sppu: BAR2 not available, DMA disabled\n");

	sppu.pdev = pdev;
	pci_set_drvdata(pdev, &sppu);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret < 0) {
		pr_warn("sppu: no IRQ, polling mode\n");
	} else {
		sppu.irq = pci_irq_vector(pdev, 0);
		ret = request_irq(sppu.irq, sppu_irq_handler,
				  IRQF_SHARED, SPPU_DEV_NAME, &sppu);
		if (ret) {
			pr_warn("sppu: request_irq failed (%d)\n", ret);
			sppu.irq = -1;
		} else {
			pr_info("sppu: IRQ %d registered\n", sppu.irq);
		}
	}

	pr_info("sppu: DEVICE_ID = 0x%04x\n",
		sppu_read32(&sppu, SPPU_REG_DEVICE_ID));
	sppu_write32(&sppu, SPPU_CTRL_RESET, SPPU_REG_CTRL);
	sppu_write32(&sppu, 0, SPPU_REG_CTRL);
	sppu_write32(&sppu, SPPU_STATUS_READY, SPPU_REG_STATUS);
	return 0;

err_regions:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void sppu_pci_remove(struct pci_dev *pdev)
{
	if (sppu.irq >= 0)
		free_irq(sppu.irq, &sppu);
	pci_free_irq_vectors(pdev);
	if (sppu.bar2) {
		pci_iounmap(pdev, sppu.bar2);
		sppu.bar2 = NULL;
	}
	if (sppu.bar0) {
		pci_iounmap(pdev, sppu.bar0);
		sppu.bar0 = NULL;
	}
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	sppu.pdev = NULL;
	pr_info("sppu: PCI device removed\n");
}

static const struct pci_device_id sppu_pci_ids[] = {
	{ PCI_DEVICE(SPPU_PCI_VENDOR_ID, SPPU_PCI_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sppu_pci_ids);

static struct pci_driver sppu_pci_driver = {
	.name       = SPPU_DEV_NAME,
	.id_table   = sppu_pci_ids,
	.probe      = sppu_pci_probe,
	.remove     = sppu_pci_remove,
};

/* ---- Module init / exit ---- */

static int __init sppu_init(void)
{
	int ret;

	pr_info("sppu: driver loading (emulation=%d)\n", emulation);
	spin_lock_init(&sppu.lock);
	init_waitqueue_head(&sppu.wait_q);
	sppu.irq = -1;

	ret = sppu_create_chardev();
	if (ret)
		return ret;

	if (emulation) {
		sppu.emu_regs = kzalloc(SPPU_BAR0_SIZE, GFP_KERNEL);
		sppu.emu_mem = vzalloc(SPPU_MAX_VECTORS * SPPU_MAX_DIM_BYTES);
		sppu.emu_target = kzalloc(SPPU_MAX_DIM_BYTES, GFP_KERNEL);
		if (!sppu.emu_regs || !sppu.emu_mem || !sppu.emu_target) {
			kfree(sppu.emu_regs);
			vfree(sppu.emu_mem);
			kfree(sppu.emu_target);
			sppu.emu_regs = NULL;
			sppu.emu_mem = NULL;
			sppu.emu_target = NULL;
			sppu_destroy_chardev();
			return -ENOMEM;
		}
		sppu.emu_regs[SPPU_REG_IDX(SPPU_REG_STATUS)] = SPPU_STATUS_READY;
		sppu.emu_regs[SPPU_REG_IDX(SPPU_REG_SEU_STATUS)] = SPPU_SEU_STATUS_READY;
		sppu.seu_done_flag = 0;
		pr_info("sppu: emulation ready (SEU v0.3), /dev/%s\n", SPPU_DEV_NAME);
	} else {
		ret = pci_register_driver(&sppu_pci_driver);
		if (ret) {
			pr_err("sppu: pci_register_driver failed\n");
			sppu_destroy_chardev();
			return ret;
		}
	}
	return 0;
}

static void __exit sppu_exit(void)
{
	if (emulation) {
		kfree(sppu.emu_regs);
		vfree(sppu.emu_mem);
		kfree(sppu.emu_target);
	} else {
		pci_unregister_driver(&sppu_pci_driver);
	}
	sppu_destroy_chardev();
	pr_info("sppu: driver unloaded\n");
}

module_init(sppu_init);
module_exit(sppu_exit);
