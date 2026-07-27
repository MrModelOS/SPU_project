#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sppu_device.h"

#define MAX_VECTORS 1000
#define MAX_DIM 768

/* Структура внутреннего состояния плаги SPPU */
typedef struct {
    uint32_t regs[8];               /* Массив регистров MMIO */
    float memory[MAX_VECTORS][MAX_DIM]; /* Внутренняя In-Memory память SPPU */
    float target_vector[MAX_DIM];   /* Искомый вектор-запрос */
} SPPUState;

static SPPUState sppu;

/* Инициализация SPPU */
void sppu_init(void) {
    memset(&sppu, 0, sizeof(SPPUState));
    sppu.regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_READY;
    printf("[SPPU Hardware] Устройство инициализировано. Статус: READY\n");
}

/* Функция записи данных в память SPPU (эмуляция DMA / загрузки контекста) */
void sppu_load_vector(uint32_t index, float *vec, uint32_t dim) {
    if (index < MAX_VECTORS && dim <= MAX_DIM) {
        memcpy(sppu.memory[index], vec, dim * sizeof(float));
    }
}

/* Ядро In-Memory вычислений: Скалярное произведение (Dot Product) */
static void sppu_run_search(void) {
    sppu.regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_BUSY;

    uint32_t count = sppu.regs[SPPU_REG_VEC_COUNT / 4];
    uint32_t dim = sppu.regs[SPPU_REG_DIMENSION / 4];

    int best_index = -1;
    float max_score = -1e9f;

    /* Вычисление сходства прямо «в памяти» SPPU */
    for (uint32_t i = 0; i < count; i++) {
        float dot_product = 0.0f;
        for (uint32_t d = 0; d < dim; d++) {
            dot_product += sppu.memory[i][d] * sppu.target_vector[d];
        }

        if (dot_product > max_score) {
            max_score = dot_product;
            best_index = i;
        }
    }

    /* Записываем результаты в регистры ответа */
    sppu.regs[SPPU_REG_RESULT_IDX / 4] = (uint32_t)best_index;
    memcpy(&sppu.regs[SPPU_REG_RESULT_SCORE / 4], &max_score, sizeof(float));

    sppu.regs[SPPU_REG_STATUS / 4] = SPPU_STATUS_DONE;
    printf("[SPPU Hardware] Поиск завершён! Лучшее совпадение: Вектор #%d (Score: %.4f)\n", 
           best_index, max_score);
}

/* Эмуляция записи в MMIO регистры (то, что делает драйвер ядра) */
void sppu_mmio_write(uint32_t offset, uint32_t value) {
    uint32_t reg_idx = offset / 4;
    sppu.regs[reg_idx] = value;

    /* Если записали флаг START в регистр CTRL — запускаем вычисления */
    if (offset == SPPU_REG_CTRL && (value & SPPU_CTRL_START)) {
        sppu_run_search();
    }
}

/* Эмуляция чтения из MMIO регистров */
uint32_t sppu_mmio_read(uint32_t offset) {
    return sppu.regs[offset / 4];
}

void sppu_set_target(float *vec, uint32_t dim) {
    memcpy(sppu.target_vector, vec, dim * sizeof(float));
}
