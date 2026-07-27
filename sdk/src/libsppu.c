/*
 * libsppu — ioctl wrapper for /dev/sppu
 */

#define _DEFAULT_SOURCE

#include "sppu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

#include "sppu_device.h"

struct sppu_handle {
	int fd;
};

sppu_t *sppu_open(const char *path)
{
	sppu_t *h;
	const char *dev = path ? path : "/dev/sppu";

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

void sppu_close(sppu_t *h)
{
	if (!h)
		return;
	if (h->fd >= 0)
		close(h->fd);
	free(h);
}

int sppu_reset(sppu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPPU_IOCTL_RESET);
}

int sppu_configure(sppu_t *h, uint32_t vec_count, uint32_t dimension)
{
	struct sppu_ioctl_param param;

	if (!h) {
		errno = EINVAL;
		return -1;
	}
	param.vec_count = vec_count;
	param.dimension = dimension;
	return ioctl(h->fd, SPPU_IOCTL_SET_PARAM, &param);
}

int sppu_load_vector(sppu_t *h, uint32_t index, const float *data, uint32_t dim)
{
	struct sppu_ioctl_vector vec;

	if (!h || !data || dim == 0 || dim > SPPU_MAX_DIMENSION) {
		errno = EINVAL;
		return -1;
	}
	memset(&vec, 0, sizeof(vec));
	vec.index = index;
	vec.dim = dim;
	memcpy(vec.data, data, dim * sizeof(float));
	return ioctl(h->fd, SPPU_IOCTL_LOAD_VEC, &vec);
}

int sppu_set_target(sppu_t *h, const float *data, uint32_t dim)
{
	struct sppu_ioctl_vector vec;

	if (!h || !data || dim == 0 || dim > SPPU_MAX_DIMENSION) {
		errno = EINVAL;
		return -1;
	}
	memset(&vec, 0, sizeof(vec));
	vec.index = 0;
	vec.dim = dim;
	memcpy(vec.data, data, dim * sizeof(float));
	return ioctl(h->fd, SPPU_IOCTL_SET_TARGET, &vec);
}

int sppu_start(sppu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPPU_IOCTL_START);
}

int sppu_get_status(sppu_t *h, uint32_t *status)
{
	if (!h || !status) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPPU_IOCTL_GET_STATUS, status);
}

int sppu_get_result(sppu_t *h, uint32_t *index, float *score, uint32_t *status)
{
	struct sppu_ioctl_result result;

	if (!h) {
		errno = EINVAL;
		return -1;
	}
	if (ioctl(h->fd, SPPU_IOCTL_GET_RESULT, &result) < 0)
		return -1;
	if (index)
		*index = result.index;
	if (score)
		*score = result.score;
	if (status)
		*status = result.status;
	return 0;
}

int sppu_wait_result(sppu_t *h, uint32_t *index, float *score,
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
		if (sppu_get_status(h, &st) < 0)
			return -1;
		if (st == SPPU_STATUS_DONE)
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

	return sppu_get_result(h, index, score, status);
}

/* ========================================================================
 * SEU — Speculative Execution Unit API
 * ======================================================================== */

int sppu_seu_configure(sppu_t *h, uint32_t depth, uint32_t offset,
		      uint32_t tree_addr, uint32_t prob_base)
{
	struct sppu_seu_config cfg;

	if (!h) {
		errno = EINVAL;
		return -1;
	}
	if (depth < SPPU_SEU_MIN_DEPTH || depth > SPPU_SEU_MAX_DEPTH) {
		errno = EINVAL;
		return -1;
	}

	cfg.depth     = depth;
	cfg.offset    = offset;
	cfg.tree_addr = tree_addr;
	cfg.prob_base = prob_base;
	return ioctl(h->fd, SPPU_IOCTL_SEU_CONFIG, &cfg);
}

int sppu_seu_start(sppu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPPU_IOCTL_SEU_START);
}

int sppu_seu_get_tree(sppu_t *h, float entries[SPPU_SEU_TREE_ENTRIES],
		     uint32_t *status)
{
	struct sppu_seu_tree_result result;
	unsigned i;

	if (!h || !entries) {
		errno = EINVAL;
		return -1;
	}

	if (ioctl(h->fd, SPPU_IOCTL_SEU_GET_TREE, &result) < 0)
		return -1;

	for (i = 0; i < SPPU_SEU_TREE_ENTRIES; i++)
		entries[i] = result.entries[i];
	if (status)
		*status = result.status;
	return 0;
}

int sppu_predict_tree(sppu_t *h, uint32_t depth, uint32_t offset,
		     float entries[SPPU_SEU_TREE_ENTRIES],
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
	if (sppu_seu_configure(h, depth, offset, 0, 0) < 0)
		return -1;
	if (sppu_seu_start(h) < 0)
		return -1;

	/* Wait for completion */
	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
		return -1;

	for (;;) {
		if (sppu_get_status(h, &st) < 0)
			return -1;

		/* Check SEU status via tree result */
		if (sppu_seu_get_tree(h, entries, &st) < 0)
			return -1;
		if (st == SPPU_SEU_STATUS_DONE)
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

/* ========================================================================
 * SEU v0.4 — Speculative Tree Walker API
 * ======================================================================== */

int sppu_seu_build_tree(sppu_t *h, uint32_t context_addr, uint32_t embed_addr,
		       uint32_t max_branches, uint32_t context_len,
		       uint32_t auto_validate, uint32_t node_base,
		       uint16_t branch_mask)
{
	struct sppu_seu_tree_context ctx;

	if (!h) {
		errno = EINVAL;
		return -1;
	}

	ctx.context_addr  = context_addr;
	ctx.embed_addr    = embed_addr;
	ctx.tree_config   = (max_branches & 0xF)
			   | ((context_len & 0xF) << 4)
			   | (auto_validate ? SPPU_TREE_CFG_AUTO_VALIDATE : 0);
	ctx.node_base_addr = node_base;
	ctx.branch_mask   = branch_mask;
	ctx._pad          = 0;

	return ioctl(h->fd, SPPU_IOCTL_SEU_BUILD_TREE, &ctx);
}

int sppu_seu_get_branches(sppu_t *h, uint16_t *branch_valid,
			 uint32_t *best_idx, float *best_score,
			 uint32_t *tree_entries)
{
	struct sppu_seu_branch_result result;

	if (!h) {
		errno = EINVAL;
		return -1;
	}

	if (ioctl(h->fd, SPPU_IOCTL_SEU_GET_BRANCHES, &result) < 0)
		return -1;

	if (branch_valid)
		*branch_valid = (uint16_t)result.branch_valid;
	if (best_idx)
		*best_idx = result.best_idx;
	if (best_score)
		memcpy(best_score, &result.best_score, sizeof(float));
	if (tree_entries)
		*tree_entries = result.tree_entries;

	return 0;
}

int sppu_seu_accept(sppu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPPU_IOCTL_SEU_ACCEPT);
}

int sppu_seu_rollback(sppu_t *h)
{
	if (!h) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SPPU_IOCTL_SEU_ROLLBACK);
}
