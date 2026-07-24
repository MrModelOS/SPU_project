/*
 * SPU (Search Processing Unit) Kernel Driver v0.2
 *
 * Dual-mode: software emulation + real PCI hardware.
 * Features:
 *   - PCI probe/remove with BAR0 MMIO + BAR2 DMA mapping
 *   - Char device /dev/spu with ioctl interface
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

#include "../emulator/spu_device.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SPU Project");
MODULE_DESCRIPTION("SPU PCI Driver with DMA, IRQ, and emulation support");
MODULE_VERSION("0.2");

static bool emulation = true;
module_param(emulation, bool, 0444);
MODULE_PARM_DESC(emulation, "Enable software emulation (no PCI hardware required)");

#define SPU_DEV_NAME   "spu"
#define SPU_CLASS_NAME "spu"
#define SPU_DMA_BAR    2
#define SPU_DMA_SIZE   (4 * 1024 * 1024)

struct spu_dev {
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
	float (*emu_mem)[SPU_MAX_DIMENSION];
	float *emu_target;
};

static struct spu_dev spu;

/* Forward declarations */
static noinline void spu_emu_run_search(struct spu_dev *s);

/* ---- MMIO accessors ---- */

static inline uint32_t spu_read32(struct spu_dev *s, uint32_t off)
{
	if (emulation)
		return s->emu_regs[SPU_REG_IDX(off)];
	return readl(s->bar0 + off);
}

static inline void spu_write32(struct spu_dev *s, uint32_t val, uint32_t off)
{
	if (emulation) {
		s->emu_regs[SPU_REG_IDX(off)] = val;
		if (off == SPU_REG_CTRL && (val & SPU_CTRL_START))
			spu_emu_run_search(s);
	} else {
		writel(val, s->bar0 + off);
	}
}

/* ---- Emulation core (dot-product in kernel FPU) ---- */

__attribute__((target("sse")))
static noinline void spu_emu_run_search(struct spu_dev *s)
{
	uint32_t count, dim;
	int best_index = -1;
	float max_score = -1.0e9f;
	uint32_t i, d;
	float dot;

	s->emu_regs[SPU_REG_IDX(SPU_REG_STATUS)] = SPU_STATUS_BUSY;

	count = s->emu_regs[SPU_REG_IDX(SPU_REG_VEC_COUNT)];
	dim   = s->emu_regs[SPU_REG_IDX(SPU_REG_DIMENSION)];

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

	s->emu_regs[SPU_REG_IDX(SPU_REG_RESULT_IDX)] = (uint32_t)best_index;
	memcpy(&s->emu_regs[SPU_REG_IDX(SPU_REG_RESULT_SCORE)],
	       &max_score, sizeof(float));
	s->emu_regs[SPU_REG_IDX(SPU_REG_STATUS)] = SPU_STATUS_DONE;
}

/* ---- Hardware-mode helpers ---- */

static void spu_hw_start_search(struct spu_dev *s)
{
	spu_write32(s, SPU_CTRL_INT_EN | SPU_CTRL_START, SPU_REG_CTRL);
}

static int spu_hw_load_vector(struct spu_dev *s, uint32_t index,
			      const float *data, uint32_t dim)
{
	size_t offset, bytes;

	if (!s->bar2)
		return -ENODEV;

	offset = SPU_MAX_DIMENSION * sizeof(float) +
		 (size_t)index * SPU_MAX_DIMENSION * sizeof(float);
	bytes  = dim * sizeof(float);

	if (offset + bytes > SPU_DMA_SIZE)
		return -EINVAL;

	memcpy_toio(s->bar2 + offset, data, bytes);
	return 0;
}

static int spu_hw_set_target(struct spu_dev *s, const float *data, uint32_t dim)
{
	size_t bytes = dim * sizeof(float);

	if (!s->bar2)
		return -ENODEV;
	if (bytes > SPU_DMA_SIZE)
		return -EINVAL;

	memcpy_toio(s->bar2, data, bytes);
	return 0;
}

static void spu_hw_get_result(struct spu_dev *s, struct spu_ioctl_result *r)
{
	uint32_t raw;

	r->index  = spu_read32(s, SPU_REG_RESULT_IDX);
	r->status = spu_read32(s, SPU_REG_STATUS);
	raw = spu_read32(s, SPU_REG_RESULT_SCORE);
	memcpy(&r->score, &raw, sizeof(float));
}

/* ---- IRQ handler ---- */

static irqreturn_t spu_irq_handler(int irq, void *dev_id)
{
	struct spu_dev *s = dev_id;
	uint32_t int_status;

	if (emulation)
		return IRQ_NONE;

	int_status = spu_read32(s, SPU_REG_INT_STATUS);
	if (!(int_status & SPU_INT_COMPLETE))
		return IRQ_NONE;

	spu_write32(s, SPU_INT_COMPLETE, SPU_REG_INT_STATUS);
	s->done_flag = 1;
	wake_up_interruptible(&s->wait_q);
	return IRQ_HANDLED;
}

static int spu_hw_wait_done(struct spu_dev *s, unsigned int timeout_ms)
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

static int spu_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &spu;
	spu.done_flag = 0;
	return 0;
}

static int spu_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long spu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct spu_dev *s = filp->private_data;
	struct spu_ioctl_param param;
	struct spu_ioctl_vector *vec = NULL;
	struct spu_ioctl_result result;
	unsigned long flags;
	int ret = 0;
	uint32_t status = 0;
	bool need_result = false;
	bool need_status = false;

	switch (cmd) {
	case SPU_IOCTL_RESET:
	case SPU_IOCTL_START:
		break;
	case SPU_IOCTL_SET_PARAM:
		if (copy_from_user(&param, (void __user *)arg, sizeof(param)))
			return -EFAULT;
		if (param.dimension > SPU_MAX_DIMENSION)
			return -EINVAL;
		break;
	case SPU_IOCTL_LOAD_VEC:
	case SPU_IOCTL_SET_TARGET:
		vec = kmalloc(sizeof(*vec), GFP_KERNEL);
		if (!vec)
			return -ENOMEM;
		if (copy_from_user(vec, (void __user *)arg, sizeof(*vec))) {
			kfree(vec);
			return -EFAULT;
		}
		if (cmd == SPU_IOCTL_LOAD_VEC) {
			if (vec->index >= SPU_MAX_VECTORS ||
			    vec->dim > SPU_MAX_DIMENSION) {
				kfree(vec);
				return -EINVAL;
			}
		} else if (vec->dim > SPU_MAX_DIMENSION) {
			kfree(vec);
			return -EINVAL;
		}
		break;
	case SPU_IOCTL_GET_RESULT:
		need_result = true;
		break;
	case SPU_IOCTL_GET_STATUS:
		need_status = true;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&s->lock, flags);

	switch (cmd) {
	case SPU_IOCTL_RESET:
		spu_write32(s, SPU_CTRL_RESET, SPU_REG_CTRL);
		spu_write32(s, 0, SPU_REG_CTRL);
		s->done_flag = 0;
		break;
	case SPU_IOCTL_SET_PARAM:
		spu_write32(s, param.vec_count, SPU_REG_VEC_COUNT);
		spu_write32(s, param.dimension, SPU_REG_DIMENSION);
		break;
	case SPU_IOCTL_LOAD_VEC:
		if (emulation) {
			memcpy(s->emu_mem[vec->index], vec->data,
			       vec->dim * sizeof(float));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			ret = spu_hw_load_vector(s, vec->index,
						  vec->data, vec->dim);
			spin_lock_irqsave(&s->lock, flags);
		}
		break;
	case SPU_IOCTL_SET_TARGET:
		if (emulation) {
			memcpy(s->emu_target, vec->data,
			       vec->dim * sizeof(float));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			ret = spu_hw_set_target(s, vec->data, vec->dim);
			spin_lock_irqsave(&s->lock, flags);
		}
		break;
	case SPU_IOCTL_START:
		if (emulation) {
			spu_write32(s, SPU_CTRL_START, SPU_REG_CTRL);
		} else {
			spu_write32(s, SPU_CTRL_INT_EN, SPU_REG_INT_MASK);
			spu_hw_start_search(s);
		}
		break;
	case SPU_IOCTL_GET_RESULT:
		if (emulation) {
			result.index = spu_read32(s, SPU_REG_RESULT_IDX);
			result.status = spu_read32(s, SPU_REG_STATUS);
			memcpy(&result.score,
			       &s->emu_regs[SPU_REG_IDX(SPU_REG_RESULT_SCORE)],
			       sizeof(float));
		} else {
			spin_unlock_irqrestore(&s->lock, flags);
			spu_hw_get_result(s, &result);
			spin_lock_irqsave(&s->lock, flags);
		}
		break;
	case SPU_IOCTL_GET_STATUS:
		status = spu_read32(s, SPU_REG_STATUS);
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

	return 0;
}

static const struct file_operations spu_fops = {
	.owner          = THIS_MODULE,
	.open           = spu_open,
	.release        = spu_release,
	.unlocked_ioctl = spu_ioctl,
	.compat_ioctl   = spu_ioctl,
};

/* ---- Chardev boilerplate ---- */

static char *spu_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int spu_create_chardev(void)
{
	int ret;
	struct class *cls;

	ret = alloc_chrdev_region(&spu.devt, 0, 1, SPU_DEV_NAME);
	if (ret) {
		pr_err("spu: alloc_chrdev_region failed\n");
		return ret;
	}

	cdev_init(&spu.cdev, &spu_fops);
	ret = cdev_add(&spu.cdev, spu.devt, 1);
	if (ret) {
		pr_err("spu: cdev_add failed\n");
		goto err_unregister;
	}

	cls = class_create(SPU_CLASS_NAME);
	if (IS_ERR(cls)) {
		ret = PTR_ERR(cls);
		goto err_cdev;
	}
	cls->devnode = spu_devnode;
	spu.class = cls;

	spu.dev = device_create(spu.class, NULL, spu.devt, NULL, SPU_DEV_NAME);
	if (IS_ERR(spu.dev)) {
		ret = PTR_ERR(spu.dev);
		goto err_class;
	}
	return 0;

err_class:
	class_destroy(spu.class);
err_cdev:
	cdev_del(&spu.cdev);
err_unregister:
	unregister_chrdev_region(spu.devt, 1);
	return ret;
}

static void spu_destroy_chardev(void)
{
	if (spu.dev)
		device_destroy(spu.class, spu.devt);
	if (spu.class)
		class_destroy(spu.class);
	cdev_del(&spu.cdev);
	unregister_chrdev_region(spu.devt, 1);
}

/* ---- PCI probe / remove ---- */

static int spu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	resource_size_t bar_start, bar_len;

	pr_info("spu: probing PCI %04x:%04x\n", pdev->vendor, id->device);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		return ret;
	}

	ret = pci_request_regions(pdev, SPU_DEV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto err_disable;
	}

	/* BAR0: MMIO registers */
	bar_start = pci_resource_start(pdev, 0);
	bar_len = pci_resource_len(pdev, 0);
	if (bar_len < SPU_BAR0_SIZE) {
		dev_err(&pdev->dev, "BAR0 too small (%pa)\n", &bar_len);
		ret = -ENODEV;
		goto err_regions;
	}

	spu.bar0 = pci_iomap(pdev, 0, SPU_BAR0_SIZE);
	if (!spu.bar0) {
		dev_err(&pdev->dev, "pci_iomap BAR0 failed\n");
		ret = -ENOMEM;
		goto err_regions;
	}

	/* BAR2: DMA buffer for vector data */
	bar_start = pci_resource_start(pdev, SPU_DMA_BAR);
	bar_len = pci_resource_len(pdev, SPU_DMA_BAR);
	if (bar_len >= SPU_DMA_SIZE) {
		spu.bar2 = pci_iomap(pdev, SPU_DMA_BAR, SPU_DMA_SIZE);
		if (spu.bar2)
			pr_info("spu: BAR2 DMA mapped %pa (%llu bytes)\n",
				&bar_start, (unsigned long long)bar_len);
	}

	if (!spu.bar2)
		pr_warn("spu: BAR2 not available, DMA disabled\n");

	spu.pdev = pdev;
	pci_set_drvdata(pdev, &spu);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret < 0) {
		pr_warn("spu: no IRQ, polling mode\n");
	} else {
		spu.irq = pci_irq_vector(pdev, 0);
		ret = request_irq(spu.irq, spu_irq_handler,
				  IRQF_SHARED, SPU_DEV_NAME, &spu);
		if (ret) {
			pr_warn("spu: request_irq failed (%d)\n", ret);
			spu.irq = -1;
		} else {
			pr_info("spu: IRQ %d registered\n", spu.irq);
		}
	}

	pr_info("spu: DEVICE_ID = 0x%04x\n",
		spu_read32(&spu, SPU_REG_DEVICE_ID));
	spu_write32(&spu, SPU_CTRL_RESET, SPU_REG_CTRL);
	spu_write32(&spu, 0, SPU_REG_CTRL);
	spu_write32(&spu, SPU_STATUS_READY, SPU_REG_STATUS);
	return 0;

err_regions:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void spu_pci_remove(struct pci_dev *pdev)
{
	if (spu.irq >= 0)
		free_irq(spu.irq, &spu);
	pci_free_irq_vectors(pdev);
	if (spu.bar2) {
		pci_iounmap(pdev, spu.bar2);
		spu.bar2 = NULL;
	}
	if (spu.bar0) {
		pci_iounmap(pdev, spu.bar0);
		spu.bar0 = NULL;
	}
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	spu.pdev = NULL;
	pr_info("spu: PCI device removed\n");
}

static const struct pci_device_id spu_pci_ids[] = {
	{ PCI_DEVICE(SPU_PCI_VENDOR_ID, SPU_PCI_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, spu_pci_ids);

static struct pci_driver spu_pci_driver = {
	.name       = SPU_DEV_NAME,
	.id_table   = spu_pci_ids,
	.probe      = spu_pci_probe,
	.remove     = spu_pci_remove,
};

/* ---- Module init / exit ---- */

static int __init spu_init(void)
{
	int ret;

	pr_info("spu: driver loading (emulation=%d)\n", emulation);
	spin_lock_init(&spu.lock);
	init_waitqueue_head(&spu.wait_q);
	spu.irq = -1;

	ret = spu_create_chardev();
	if (ret)
		return ret;

	if (emulation) {
		spu.emu_regs = kzalloc(SPU_BAR0_SIZE, GFP_KERNEL);
		spu.emu_mem = vzalloc(SPU_MAX_VECTORS * SPU_MAX_DIM_BYTES);
		spu.emu_target = kzalloc(SPU_MAX_DIM_BYTES, GFP_KERNEL);
		if (!spu.emu_regs || !spu.emu_mem || !spu.emu_target) {
			kfree(spu.emu_regs);
			vfree(spu.emu_mem);
			kfree(spu.emu_target);
			spu.emu_regs = NULL;
			spu.emu_mem = NULL;
			spu.emu_target = NULL;
			spu_destroy_chardev();
			return -ENOMEM;
		}
		spu.emu_regs[SPU_REG_IDX(SPU_REG_STATUS)] = SPU_STATUS_READY;
		pr_info("spu: emulation ready, /dev/%s\n", SPU_DEV_NAME);
	} else {
		ret = pci_register_driver(&spu_pci_driver);
		if (ret) {
			pr_err("spu: pci_register_driver failed\n");
			spu_destroy_chardev();
			return ret;
		}
	}
	return 0;
}

static void __exit spu_exit(void)
{
	if (emulation) {
		kfree(spu.emu_regs);
		vfree(spu.emu_mem);
		kfree(spu.emu_target);
	} else {
		pci_unregister_driver(&spu_pci_driver);
	}
	spu_destroy_chardev();
	pr_info("spu: driver unloaded\n");
}

module_init(spu_init);
module_exit(spu_exit);
