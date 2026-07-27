/*
 * SPPU Userspace Test — /dev/sppu ioctl client
 * C11, POSIX
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "../emulator/sppu_device.h"

static float frand(void)
{
	return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

int main(void)
{
	int fd = open("/dev/sppu", O_RDWR);
	int exit_code = 1;

	if (fd < 0) {
		perror("open /dev/sppu");
		return 1;
	}

	struct sppu_ioctl_param param = {
		.vec_count  = 10,
		.dimension  = 8,
	};
	struct sppu_ioctl_vector vec;
	struct sppu_ioctl_result result;
	float target[8];
	int i, d;
	uint32_t status;

	srand((unsigned)time(NULL));

	printf("=== SPPU Userspace Test ===\n");

	if (ioctl(fd, SPPU_IOCTL_RESET) < 0) {
		perror("ioctl reset");
		goto out;
	}

	if (ioctl(fd, SPPU_IOCTL_SET_PARAM, &param) < 0) {
		perror("ioctl set_param");
		goto out;
	}

	/* 1. Генерируем 10 случайных векторов */
	for (i = 0; i < 10; i++) {
		vec.index = (uint32_t)i;
		vec.dim   = 8;
		for (d = 0; d < 8; d++)
			vec.data[d] = frand();
		if (ioctl(fd, SPPU_IOCTL_LOAD_VEC, &vec) < 0) {
			perror("ioctl load_vec");
			goto out;
		}
	}

	/* 2. Копируем target в индекс #7 (validation trap) */
	for (d = 0; d < 8; d++)
		target[d] = frand();

	memcpy(vec.data, target, sizeof(float) * 8);
	vec.index = 7;
	vec.dim   = 8;
	if (ioctl(fd, SPPU_IOCTL_LOAD_VEC, &vec) < 0) {
		perror("ioctl load_vec #7");
		goto out;
	}

	/* 3. Устанавливаем target-вектор запроса */
	memcpy(vec.data, target, sizeof(float) * 8);
	vec.index = 0; /* не используется для target */
	vec.dim   = 8;
	if (ioctl(fd, SPPU_IOCTL_SET_TARGET, &vec) < 0) {
		perror("ioctl set_target");
		goto out;
	}

	/* 4. Запускаем поиск */
	if (ioctl(fd, SPPU_IOCTL_START) < 0) {
		perror("ioctl start");
		goto out;
	}

	/* 5. Polling статуса до DONE */
	do {
		if (ioctl(fd, SPPU_IOCTL_GET_STATUS, &status) < 0) {
			perror("ioctl get_status");
			goto out;
		}
		usleep(500);
	} while (status != SPPU_STATUS_DONE);

	/* 6. Читаем результат */
	if (ioctl(fd, SPPU_IOCTL_GET_RESULT, &result) < 0) {
		perror("ioctl get_result");
		goto out;
	}

	printf("Result: index=%u  score=%.6f  status=%u\n",
	       result.index, result.score, result.status);

	if (result.index == 7) {
		printf("Validation PASSED — exact match at expected index #7.\n");
		exit_code = 0;
	} else {
		printf("Validation FAILED — expected #7, got %u\n", result.index);
	}

out:
	close(fd);
	return exit_code;
}
