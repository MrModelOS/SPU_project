#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "spu_device.h"

#define MAX_VECTORS 1000
#define MAX_DIM 768

/* Структура внутреннего состояния плаги SPU */
typedef struct {
    uint32_t regs[8];               /* Массив регистров MMIO */
    float memory[MAX_VECTORS][MAX_DIM]; /* Внутренняя In-Memory память SPU */
    float target_vector[MAX_DIM];   /* Искомый вектор-запрос */
} SPUState;

static SPUState spu;

/* Инициализация SPU */
void spu_init(void) {
    memset(&spu, 0, sizeof(SPUState));
    spu.regs[SPU_REG_STATUS / 4] = SPU_STATUS_READY;
    printf("[SPU Hardware] Устройство инициализировано. Статус: READY\n");
}

/* Функция записи данных в память SPU (эмуляция DMA / загрузки контекста) */
void spu_load_vector(uint32_t index, float *vec, uint32_t dim) {
    if (index < MAX_VECTORS && dim <= MAX_DIM) {
        memcpy(spu.memory[index], vec, dim * sizeof(float));
    }
}

/* Ядро In-Memory вычислений: Скалярное произведение (Dot Product) */
static void spu_run_search(void) {
    spu.regs[SPU_REG_STATUS / 4] = SPU_STATUS_BUSY;

    uint32_t count = spu.regs[SPU_REG_VEC_COUNT / 4];
    uint32_t dim = spu.regs[SPU_REG_DIMENSION / 4];

    int best_index = -1;
    float max_score = -1e9f;

    /* Вычисление сходства прямо «в памяти» SPU */
    for (uint32_t i = 0; i < count; i++) {
        float dot_product = 0.0f;
        for (uint32_t d = 0; d < dim; d++) {
            dot_product += spu.memory[i][d] * spu.target_vector[d];
        }

        if (dot_product > max_score) {
            max_score = dot_product;
            best_index = i;
        }
    }

    /* Записываем результаты в регистры ответа */
    spu.regs[SPU_REG_RESULT_IDX / 4] = (uint32_t)best_index;
    memcpy(&spu.regs[SPU_REG_RESULT_SCORE / 4], &max_score, sizeof(float));

    spu.regs[SPU_REG_STATUS / 4] = SPU_STATUS_DONE;
    printf("[SPU Hardware] Поиск завершён! Лучшее совпадение: Вектор #%d (Score: %.4f)\n", 
           best_index, max_score);
}

/* Эмуляция записи в MMIO регистры (то, что делает драйвер ядра) */
void spu_mmio_write(uint32_t offset, uint32_t value) {
    uint32_t reg_idx = offset / 4;
    spu.regs[reg_idx] = value;

    /* Если записали флаг START в регистр CTRL — запускаем вычисления */
    if (offset == SPU_REG_CTRL && (value & SPU_CTRL_START)) {
        spu_run_search();
    }
}

/* Эмуляция чтения из MMIO регистров */
uint32_t spu_mmio_read(uint32_t offset) {
    return spu.regs[offset / 4];
}

void spu_set_target(float *vec, uint32_t dim) {
    memcpy(spu.target_vector, vec, dim * sizeof(float));
}
