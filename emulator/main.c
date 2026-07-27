/*
 * SPPU Emulator — Testbench (main.c)
 * C11, strict alignment aware.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "sppu_device.h"

/* Forward declarations — API из sppu_pci.c */
extern void sppu_init(void);
extern void sppu_load_vector(uint32_t index, float *vec, uint32_t dim);
extern void sppu_mmio_write(uint32_t offset, uint32_t value);
extern uint32_t sppu_mmio_read(uint32_t offset);
extern void sppu_set_target(float *vec, uint32_t dim);

#define N_VECS 10U
#define DIM    8U

static float frand(void)
{
	return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

int main(void)
{
	float vectors[N_VECS][DIM];
	float target[DIM];
	uint32_t best_idx;
	uint32_t score_bits;
	float best_score;

	srand((unsigned)time(NULL));

	printf("=== SPPU Emulator Testbench ===\n");

	/* 1. Генерация случайного вектора-запроса */
	for (uint32_t d = 0; d < DIM; d++)
		target[d] = frand();

	printf("Target vector : [");
	for (uint32_t d = 0; d < DIM; d++)
		printf(" %.3f", target[d]);
	printf(" ]\n\n");

	/* 2. Генерация 10 случайных векторов в банк памяти SPPU.
	 * Для валидации копируем target в индекс 7 — ожидаем, что SPPU
	 * найдёт именно его с максимальным dot-product.
	 */
	for (uint32_t i = 0; i < N_VECS; i++) {
		for (uint32_t d = 0; d < DIM; d++)
			vectors[i][d] = frand();
	}
	memcpy(vectors[7], target, sizeof(float) * DIM);

	/* 3. Инициализация «железа» */
	sppu_init();

	/* 4. Загрузка векторов в In-Memory банк (эмуляция DMA) */
	for (uint32_t i = 0; i < N_VECS; i++) {
		sppu_load_vector(i, vectors[i], DIM);
		printf("Loaded vec #%2u : [", i);
		for (uint32_t d = 0; d < DIM; d++)
			printf(" %.3f", vectors[i][d]);
		printf(" ]\n");
	}

	/* 5. Настройка MMIO-регистров перед стартом */
	sppu_mmio_write(SPPU_REG_VEC_COUNT, N_VECS);
	sppu_mmio_write(SPPU_REG_DIMENSION, DIM);
	sppu_set_target(target, DIM);

	/* 6. Запуск поиска — пишем флаг SPPU_CTRL_START */
	sppu_mmio_write(SPPU_REG_CTRL, SPPU_CTRL_START);

	/* 7. Polling статуса (в QEMU/эмуляции синхронно, но оставляем
	 * цикл для соответствия реальному MMIO-flow драйвера ядра)
	 */
	while (sppu_mmio_read(SPPU_REG_STATUS) != SPPU_STATUS_DONE)
		/* busy-wait */;

	/* 8. Чтение результата */
	best_idx    = sppu_mmio_read(SPPU_REG_RESULT_IDX);
	score_bits  = sppu_mmio_read(SPPU_REG_RESULT_SCORE);

	/* Переинтерпретация бит float из MMIO в соответствии с C11 */
	memcpy(&best_score, &score_bits, sizeof(float));

	/* 9. Вывод результатов */
	printf("\n=== Search Complete ===\n");
	printf("Best index            : %u\n", best_idx);
	printf("Similarity (dot-prod) : %.6f\n", best_score);

	if (best_idx == 7) {
		printf("Validation PASSED — exact match at expected index #7.\n");
	} else {
		printf("Validation INFO — index %u won (expected #7).\n", best_idx);
	}

	return 0;
}
