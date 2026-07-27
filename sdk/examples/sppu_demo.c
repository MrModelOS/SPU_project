/*
 * sppu_demo — validation trap via libsppu (exact match at index 7)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "sppu.h"

static float frand(void)
{
	return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

int main(void)
{
	sppu_t *sppu;
	float bank[10][8];
	float target[8];
	uint32_t index = 0, status = 0;
	float score = 0.0f;
	int i, d;
	int exit_code = 1;

	srand((unsigned)time(NULL));

	printf("=== SPPU SDK Demo (libsppu) ===\n");

	sppu = sppu_open(NULL);
	if (!sppu) {
		perror("sppu_open");
		return 1;
	}

	if (sppu_reset(sppu) < 0) {
		perror("sppu_reset");
		goto out;
	}

	if (sppu_configure(sppu, 10, 8) < 0) {
		perror("sppu_configure");
		goto out;
	}

	for (i = 0; i < 10; i++) {
		for (d = 0; d < 8; d++)
			bank[i][d] = frand();
		if (sppu_load_vector(sppu, (uint32_t)i, bank[i], 8) < 0) {
			perror("sppu_load_vector");
			goto out;
		}
	}

	for (d = 0; d < 8; d++)
		target[d] = frand();

	memcpy(bank[7], target, sizeof(target));
	if (sppu_load_vector(sppu, 7, bank[7], 8) < 0) {
		perror("sppu_load_vector #7");
		goto out;
	}

	if (sppu_set_target(sppu, target, 8) < 0) {
		perror("sppu_set_target");
		goto out;
	}

	if (sppu_start(sppu) < 0) {
		perror("sppu_start");
		goto out;
	}

	if (sppu_wait_result(sppu, &index, &score, &status, 5000) < 0) {
		perror("sppu_wait_result");
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
	sppu_close(sppu);
	return exit_code;
}
