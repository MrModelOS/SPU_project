/*
 * SPU (Search Processing Unit) Kernel Driver
 *
 * Функции:
 *  - PCI probe/remove с маппингом BAR0 через ioremap
 *  - Char device /dev/spu с ioctl интерфейсом
 *  - Software-эмуляция режим (emulation=1) для тестирования без PCI hardware
 *
 * Стайл: Linux Kernel Coding Style, C11 (kernel subset)
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
#include <linux/string.h>
#include <linux/ioctl.h>

#include "../emulator/spu_device.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SPU Project");
MODULE_DESCRIPTION("SPU PCI Driver with MMIO and emulation support");
MODULE_VERSION("0.1");

static bool emulation = true;
module_param(emulation, bool, 0444);
MODULE_PARM_DESC(emulation,
		 "Enable software emulation (no PCI hardware required)");

#define SPU_DEV_NAME	"spu"
#define SPU_CLASS_NAME	"spu"

/*
 * Один экземпляр устройства на весь модуль (single-function SPU).
 * Для multi-function топологии превратить в список struct spu_device.
 */
struct spu_dev {
	struct pci_dev *pdev;		/* NULL в режиме emulation       */
	void __iomem *bar0;		/* iomapped BAR0                 */
	struct cdev cdev;
	struct device *dev;
	dev_t devt;
	struct class *class;
	spinlock_t lock;

	/* Emulation buffers (используются только при emulation=1) */
	uint32_t *emu_regs;
	float (*emu_mem)[SPU_MAX_DIMENSION];
	float *emu_target;
};

static struct spu_dev spu;

/*
 * ==========================================================================
 * Emulation core (software SPU running inside kernel)
 * ==========================================================================
 */
#include <asm/fpu/api.h>

/*
 * target("sse") required because the kernel is compiled with -mno-sse.
 * Runtime SSE usage is guarded with kernel_fpu_begin/end.
 */
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

	pr_info("spu: emulation search done, best=%d score=%d (x1000)\n",
		best_index, (int)(max_score * 1000.0f));
}

/*
 * ==========================================================================
 * MMIO accessors — unified for HW and emulation
 * ==========================================================================
 */
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

/*
 * ==========================================================================
 * Chardev file operations
 * ==========================================================================
 */
static int spu_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &spu;
	pr_info("spu: opened by pid %d\n", current->pid);
	return 0;
}

static int spu_release(struct inode *inode, struct file *filp)
{
	pr_info("spu: closed by pid %d\n", current->pid);
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

	/* User copies happen outside the spinlock (may sleep). */
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
			/* TODO: DMA или программная запись в MMIO window */
			ret = -ENOSYS;
		}
		break;

	case SPU_IOCTL_SET_TARGET:
		if (emulation)
			memcpy(s->emu_target, vec->data, vec->dim * sizeof(float));
		else
			ret = -ENOSYS;
		break;

	case SPU_IOCTL_START:
		spu_write32(s, SPU_CTRL_START, SPU_REG_CTRL);
		break;

	case SPU_IOCTL_GET_RESULT:
		result.index = spu_read32(s, SPU_REG_RESULT_IDX);
		result.status = spu_read32(s, SPU_REG_STATUS);
		if (emulation) {
			memcpy(&result.score,
			       &s->emu_regs[SPU_REG_IDX(SPU_REG_RESULT_SCORE)],
			       sizeof(float));
		} else {
			uint32_t raw;

			raw = spu_read32(s, SPU_REG_RESULT_SCORE);
			memcpy(&result.score, &raw, sizeof(float));
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
	.owner		= THIS_MODULE,
	.open		= spu_open,
	.release	= spu_release,
	.unlocked_ioctl	= spu_ioctl,
	.compat_ioctl	= spu_ioctl,
};

/*
 * ==========================================================================
 * Chardev / sysfs boilerplate
 * ==========================================================================
 */
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
		pr_err("spu: class_create failed\n");
		goto err_cdev;
	}
	cls->devnode = spu_devnode;
	spu.class = cls;

	spu.dev = device_create(spu.class, NULL, spu.devt, NULL, SPU_DEV_NAME);
	if (IS_ERR(spu.dev)) {
		ret = PTR_ERR(spu.dev);
		pr_err("spu: device_create failed\n");
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

/*
 * ==========================================================================
 * PCI probe / remove
 * ==========================================================================
 */
static int spu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	resource_size_t bar_start, bar_len;
	uint32_t dev_id;

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

	bar_start = pci_resource_start(pdev, 0);
	bar_len = pci_resource_len(pdev, 0);
	if (bar_len < SPU_BAR0_SIZE) {
		dev_err(&pdev->dev, "BAR0 too small (%pa)\n", &bar_len);
		ret = -ENODEV;
		goto err_regions;
	}

	spu.bar0 = pci_iomap(pdev, 0, SPU_BAR0_SIZE);
	if (!spu.bar0) {
		dev_err(&pdev->dev, "pci_iomap failed\n");
		ret = -ENOMEM;
		goto err_regions;
	}

	spu.pdev = pdev;
	pci_set_drvdata(pdev, &spu);

	pr_info("spu: BAR0 mapped at %pa, size %u\n", &bar_start, SPU_BAR0_SIZE);

	/* Sanity check: считать DEVICE_ID из MMIO */
	dev_id = spu_read32(&spu, SPU_REG_DEVICE_ID);
	pr_info("spu: MMIO DEVICE_ID = 0x%04x\n", dev_id);

	/* Сброс устройства */
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
	.name		= SPU_DEV_NAME,
	.id_table	= spu_pci_ids,
	.probe		= spu_pci_probe,
	.remove		= spu_pci_remove,
};

/*
 * ==========================================================================
 * Module init / exit
 * ==========================================================================
 */
static int __init spu_init(void)
{
	int ret;

	pr_info("spu: driver loading (emulation=%d)\n", emulation);
	spin_lock_init(&spu.lock);

	ret = spu_create_chardev();
	if (ret)
		return ret;

	if (emulation) {
		pr_info("spu: allocating emulation state\n");
		spu.emu_regs = kzalloc(SPU_BAR0_SIZE, GFP_KERNEL);
		spu.emu_mem = vzalloc(SPU_MAX_VECTORS * SPU_MAX_DIM_BYTES);
		spu.emu_target = kzalloc(SPU_MAX_DIM_BYTES, GFP_KERNEL);
		if (!spu.emu_regs || !spu.emu_mem || !spu.emu_target) {
			pr_err("spu: emulation alloc failed\n");
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
