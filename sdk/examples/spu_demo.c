/*
 * spu_demo — validation trap via libspu (exact match at index 7)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "spu.h"

static float frand(void)
{
	return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

int main(void)
{
	spu_t *spu;
	float bank[10][8];
	float target[8];
	uint32_t index = 0, status = 0;
	float score = 0.0f;
	int i, d;
	int exit_code = 1;

	srand((unsigned)time(NULL));

	printf("=== SPU SDK Demo (libspu) ===\n");

	spu = spu_open(NULL);
	if (!spu) {
		perror("spu_open");
		return 1;
	}

	if (spu_reset(spu) < 0) {
		perror("spu_reset");
		goto out;
	}

	if (spu_configure(spu, 10, 8) < 0) {
		perror("spu_configure");
		goto out;
	}

	for (i = 0; i < 10; i++) {
		for (d = 0; d < 8; d++)
			bank[i][d] = frand();
		if (spu_load_vector(spu, (uint32_t)i, bank[i], 8) < 0) {
			perror("spu_load_vector");
			goto out;
		}
	}

	for (d = 0; d < 8; d++)
		target[d] = frand();

	memcpy(bank[7], target, sizeof(target));
	if (spu_load_vector(spu, 7, bank[7], 8) < 0) {
		perror("spu_load_vector #7");
		goto out;
	}

	if (spu_set_target(spu, target, 8) < 0) {
		perror("spu_set_target");
		goto out;
	}

	if (spu_start(spu) < 0) {
		perror("spu_start");
		goto out;
	}

	if (spu_wait_result(spu, &index, &score, &status, 5000) < 0) {
		perror("spu_wait_result");
		goto out;
	}

	printf("Result: index=%u  score=%.6f  status=%u\n",
	       index, score, status);

	if (index == 7) {
		printf("Validation PASSED — exact match at expected index #7.\n");
		exit_code = 0;
	} else {
		printf("Validation FAILED — expected #7, got %u\n", index);
	}

out:
	spu_close(spu);
	return exit_code;
}
