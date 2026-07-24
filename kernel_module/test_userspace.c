/*
 * SPU Userspace Test — /dev/spu ioctl client
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

#include "../emulator/spu_device.h"

static float frand(void)
{
	return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

int main(void)
{
	int fd = open("/dev/spu", O_RDWR);
	int exit_code = 1;

	if (fd < 0) {
		perror("open /dev/spu");
		return 1;
	}

	struct spu_ioctl_param param = {
		.vec_count  = 10,
		.dimension  = 8,
	};
	struct spu_ioctl_vector vec;
	struct spu_ioctl_result result;
	float target[8];
	int i, d;
	uint32_t status;

	srand((unsigned)time(NULL));

	printf("=== SPU Userspace Test ===\n");

	if (ioctl(fd, SPU_IOCTL_RESET) < 0) {
		perror("ioctl reset");
		goto out;
	}

	if (ioctl(fd, SPU_IOCTL_SET_PARAM, &param) < 0) {
		perror("ioctl set_param");
		goto out;
	}

	/* 1. Генерируем 10 случайных векторов */
	for (i = 0; i < 10; i++) {
		vec.index = (uint32_t)i;
		vec.dim   = 8;
		for (d = 0; d < 8; d++)
			vec.data[d] = frand();
		if (ioctl(fd, SPU_IOCTL_LOAD_VEC, &vec) < 0) {
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
	if (ioctl(fd, SPU_IOCTL_LOAD_VEC, &vec) < 0) {
		perror("ioctl load_vec #7");
		goto out;
	}

	/* 3. Устанавливаем target-вектор запроса */
	memcpy(vec.data, target, sizeof(float) * 8);
	vec.index = 0; /* не используется для target */
	vec.dim   = 8;
	if (ioctl(fd, SPU_IOCTL_SET_TARGET, &vec) < 0) {
		perror("ioctl set_target");
		goto out;
	}

	/* 4. Запускаем поиск */
	if (ioctl(fd, SPU_IOCTL_START) < 0) {
		perror("ioctl start");
		goto out;
	}

	/* 5. Polling статуса до DONE */
	do {
		if (ioctl(fd, SPU_IOCTL_GET_STATUS, &status) < 0) {
			perror("ioctl get_status");
			goto out;
		}
		usleep(500);
	} while (status != SPU_STATUS_DONE);

	/* 6. Читаем результат */
	if (ioctl(fd, SPU_IOCTL_GET_RESULT, &result) < 0) {
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
