/*
 * SPU Device Header
 * Карта MMIO-регистров и константы Search Processing Unit
 *
 * Совместимость: C11 userspace / Linux Kernel (__KERNEL__)
 * Выравнивание: все 32-битные регистры выровнены по границе 4 байт.
 */

#ifndef SPU_DEVICE_H
#define SPU_DEVICE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/* ==========================================================================
 * PCI Express идентификаторы
 * ========================================================================== */
#define SPU_PCI_VENDOR_ID	0x1234U
#define SPU_PCI_DEVICE_ID	0x5780U	/* SPU Generation 0 */

/* ==========================================================================
 * BAR0: MMIO Register Space
 * ========================================================================== */
#define SPU_BAR0_SIZE		0x1000U	/* 4 KiB MMIO window */

/*
 * Карта регистров (смещения от BASE + 0)
 * Все смещения кратны 4.
 */
#define SPU_REG_CTRL		0x00U	/* RW: Управление (start/reset/irq_en) */
#define SPU_REG_STATUS		0x04U	/* RO: Статус устройства               */
#define SPU_REG_VEC_COUNT	0x08U	/* RW: Количество векторов в банке     */
#define SPU_REG_DIMENSION	0x0CU	/* RW: Размерность вектора (dim)       */
#define SPU_REG_TARGET_ADDR	0x10U	/* RW: Базовый адрес target-вектора*   */
#define SPU_REG_RESULT_IDX	0x18U	/* RO: Индекс наилучшего совпадения    */
#define SPU_REG_RESULT_SCORE	0x1CU	/* RO: Оценка сходства (raw float)     */
#define SPU_REG_DEVICE_ID	0x20U	/* RO: ID устройства / версия прошивки */
#define SPU_REG_INT_MASK	0x24U	/* RW: Маска прерываний                */
#define SPU_REG_INT_STATUS	0x28U	/* RW1C: Статус прерываний             */

/* ==========================================================================
 * Флаги управления — SPU_REG_CTRL
 * ========================================================================== */
#define SPU_CTRL_START		(1U << 0)	/* Запуск поиска (писать 1)   */
#define SPU_CTRL_RESET		(1U << 1)	/* Сброс конвейера и FIFO     */
#define SPU_CTRL_INT_EN		(1U << 2)	/* Разрешить прерывание DONE  */

/* ==========================================================================
 * Коды статуса — SPU_REG_STATUS
 * ========================================================================== */
#define SPU_STATUS_READY	0x00U
#define SPU_STATUS_BUSY		0x01U
#define SPU_STATUS_DONE		0x02U
#define SPU_STATUS_ERR_DMA	0x03U

/* ==========================================================================
 * Прерывания — SPU_REG_INT_STATUS / SPU_REG_INT_MASK
 * ========================================================================== */
#define SPU_INT_COMPLETE	(1U << 0)
#define SPU_INT_ERR_DMA		(1U << 1)

/* ==========================================================================
 * Лимиты памяти и данных
 * ========================================================================== */
#define SPU_MAX_VECTORS		1000U
#define SPU_MAX_DIMENSION	768U
#define SPU_MAX_DIM_BYTES	(SPU_MAX_DIMENSION * sizeof(float))

/* ==========================================================================
 * Вспомогательные макросы
 * ========================================================================== */
#define SPU_REG_IDX(off)	((off) / sizeof(uint32_t))
#define SPU_REG_ALIGNED(off)	(((off) & 0x3U) == 0U)

/* ==========================================================================
 * Userspace <-> Kernel ioctl interface
 * ========================================================================== */
#ifndef __KERNEL__
#include <sys/ioctl.h>
#else
#include <linux/ioctl.h>
#endif

struct spu_ioctl_param {
	uint32_t vec_count;
	uint32_t dimension;
};

struct spu_ioctl_vector {
	uint32_t index;
	uint32_t dim;
	float data[SPU_MAX_DIMENSION];
};

struct spu_ioctl_result {
	uint32_t index;
	float score;
	uint32_t status;
};

#define SPU_IOCTL_MAGIC		'S'
#define SPU_IOCTL_RESET		_IO(SPU_IOCTL_MAGIC, 0)
#define SPU_IOCTL_SET_PARAM	_IOW(SPU_IOCTL_MAGIC, 1, struct spu_ioctl_param)
#define SPU_IOCTL_LOAD_VEC	_IOW(SPU_IOCTL_MAGIC, 2, struct spu_ioctl_vector)
#define SPU_IOCTL_SET_TARGET	_IOW(SPU_IOCTL_MAGIC, 3, struct spu_ioctl_vector)
#define SPU_IOCTL_START		_IO(SPU_IOCTL_MAGIC, 4)
#define SPU_IOCTL_GET_RESULT	_IOR(SPU_IOCTL_MAGIC, 5, struct spu_ioctl_result)
#define SPU_IOCTL_GET_STATUS	_IOR(SPU_IOCTL_MAGIC, 6, uint32_t)

#endif /* SPU_DEVICE_H */
