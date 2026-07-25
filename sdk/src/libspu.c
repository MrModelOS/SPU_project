/*
 * libspu — ioctl wrapper for /dev/spu
 */

#define _DEFAULT_SOURCE

#include "spu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

#include "spu_device.h"

struct spu_handle {
	int fd;
};

spu_t *spu_open(const char *path)
{
	spu_t *h;
	const char *dev = path ? path : "/dev/spu";

	h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;

	h->fd = open(dev, O_RDWR);
	if (h->fd < 0) {
		free(h);
		return NULL;
	}
	return h;
}

void spu_close(spu_t *h)
{
	if (!h)
		return;
	if (h->fd >= 0)
		close(h->fd);
	free(h);
}

int spu_reset(spu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPU_IOCTL_RESET);
}

int spu_configure(spu_t *h, uint32_t vec_count, uint32_t dimension)
{
	struct spu_ioctl_param param;

	if (!h) {
		errno = EINVAL;
		return -1;
	}
	param.vec_count = vec_count;
	param.dimension = dimension;
	return ioctl(h->fd, SPU_IOCTL_SET_PARAM, &param);
}

int spu_load_vector(spu_t *h, uint32_t index, const float *data, uint32_t dim)
{
	struct spu_ioctl_vector vec;

	if (!h || !data || dim == 0 || dim > SPU_MAX_DIMENSION) {
		errno = EINVAL;
		return -1;
	}
	memset(&vec, 0, sizeof(vec));
	vec.index = index;
	vec.dim = dim;
	memcpy(vec.data, data, dim * sizeof(float));
	return ioctl(h->fd, SPU_IOCTL_LOAD_VEC, &vec);
}

int spu_set_target(spu_t *h, const float *data, uint32_t dim)
{
	struct spu_ioctl_vector vec;

	if (!h || !data || dim == 0 || dim > SPU_MAX_DIMENSION) {
		errno = EINVAL;
		return -1;
	}
	memset(&vec, 0, sizeof(vec));
	vec.index = 0;
	vec.dim = dim;
	memcpy(vec.data, data, dim * sizeof(float));
	return ioctl(h->fd, SPU_IOCTL_SET_TARGET, &vec);
}

int spu_start(spu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPU_IOCTL_START);
}

int spu_get_status(spu_t *h, uint32_t *status)
{
	if (!h || !status) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPU_IOCTL_GET_STATUS, status);
}

int spu_get_result(spu_t *h, uint32_t *index, float *score, uint32_t *status)
{
	struct spu_ioctl_result result;

	if (!h) {
		errno = EINVAL;
		return -1;
	}
	if (ioctl(h->fd, SPU_IOCTL_GET_RESULT, &result) < 0)
		return -1;
	if (index)
		*index = result.index;
	if (score)
		*score = result.score;
	if (status)
		*status = result.status;
	return 0;
}

int spu_wait_result(spu_t *h, uint32_t *index, float *score,
		    uint32_t *status, unsigned timeout_ms)
{
	uint32_t st;
	struct timespec start, now;
	unsigned elapsed_ms;

	if (!h) {
		errno = EINVAL;
		return -1;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
		return -1;

	for (;;) {
		if (spu_get_status(h, &st) < 0)
			return -1;
		if (st == SPU_STATUS_DONE)
			break;

		if (timeout_ms > 0) {
			if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
				return -1;
			elapsed_ms = (unsigned)((now.tv_sec - start.tv_sec) * 1000u +
						(now.tv_nsec - start.tv_nsec) / 1000000L);
			if (elapsed_ms >= timeout_ms) {
				errno = ETIMEDOUT;
				return -1;
			}
		}
		usleep(500);
	}

	return spu_get_result(h, index, score, status);
}

/* ========================================================================
 * SEU — Speculative Execution Unit API
 * ======================================================================== */

int spu_seu_configure(spu_t *h, uint32_t depth, uint32_t offset,
		      uint32_t tree_addr, uint32_t prob_base)
{
	struct spu_seu_config cfg;

	if (!h) {
		errno = EINVAL;
		return -1;
	}
	if (depth < SPU_SEU_MIN_DEPTH || depth > SPU_SEU_MAX_DEPTH) {
		errno = EINVAL;
		return -1;
	}

	cfg.depth     = depth;
	cfg.offset    = offset;
	cfg.tree_addr = tree_addr;
	cfg.prob_base = prob_base;
	return ioctl(h->fd, SPU_IOCTL_SEU_CONFIG, &cfg);
}

int spu_seu_start(spu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPU_IOCTL_SEU_START);
}

int spu_seu_get_tree(spu_t *h, float entries[SPU_SEU_TREE_ENTRIES],
		     uint32_t *status)
{
	struct spu_seu_tree_result result;
	int i;

	if (!h || !entries) {
		errno = EINVAL;
		return -1;
	}

	if (ioctl(h->fd, SPU_IOCTL_SEU_GET_TREE, &result) < 0)
		return -1;

	for (i = 0; i < SPU_SEU_TREE_ENTRIES; i++)
		entries[i] = result.entries[i];
	if (status)
		*status = result.status;
	return 0;
}

int spu_predict_tree(spu_t *h, uint32_t depth, uint32_t offset,
		     float entries[SPU_SEU_TREE_ENTRIES],
		     unsigned timeout_ms)
{
	uint32_t st;
	struct timespec start, now;
	unsigned elapsed_ms;

	if (!h || !entries) {
		errno = EINVAL;
		return -1;
	}

	/* Configure and start */
	if (spu_seu_configure(h, depth, offset, 0, 0) < 0)
		return -1;
	if (spu_seu_start(h) < 0)
		return -1;

	/* Wait for completion */
	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
		return -1;

	for (;;) {
		if (spu_get_status(h, &st) < 0)
			return -1;

		/* Check SEU status via tree result */
		if (spu_seu_get_tree(h, entries, &st) < 0)
			return -1;
		if (st == SPU_SEU_STATUS_DONE)
			break;

		if (timeout_ms > 0) {
			if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
				return -1;
			elapsed_ms = (unsigned)((now.tv_sec - start.tv_sec) * 1000u +
						(now.tv_nsec - start.tv_nsec) / 1000000L);
			if (elapsed_ms >= timeout_ms) {
				errno = ETIMEDOUT;
				return -1;
			}
		}
		usleep(500);
	}

	return 0;
}
