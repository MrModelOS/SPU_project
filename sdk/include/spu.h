/**
 * libspu — userspace SDK for /dev/spu
 *
 * Thin C wrapper over the SPU/SEU ioctl ABI defined in spu_device.h.
 */

#ifndef SPU_H
#define SPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spu_handle spu_t;

/**
 * Open an SPU device.
 * @param path  Device path, or NULL for "/dev/spu"
 * @return handle on success, NULL on error (errno set)
 */
spu_t *spu_open(const char *path);

/** Close handle and free resources. Safe with NULL. */
void spu_close(spu_t *h);

int spu_reset(spu_t *h);
int spu_configure(spu_t *h, uint32_t vec_count, uint32_t dimension);
int spu_load_vector(spu_t *h, uint32_t index, const float *data, uint32_t dim);
int spu_set_target(spu_t *h, const float *data, uint32_t dim);
int spu_start(spu_t *h);
int spu_get_status(spu_t *h, uint32_t *status);
int spu_get_result(spu_t *h, uint32_t *index, float *score, uint32_t *status);

/**
 * Poll STATUS until DONE (or timeout), then read the result.
 * @param timeout_ms  0 means wait indefinitely
 * @return 0 on success, -1 on error (errno: ETIMEDOUT on timeout)
 */
int spu_wait_result(spu_t *h, uint32_t *index, float *score,
		    uint32_t *status, unsigned timeout_ms);

/* ========================================================================
 * SEU (Speculative Execution Unit) — Prediction Tree API
 * ======================================================================== */

#define SPU_SEU_MIN_DEPTH  5
#define SPU_SEU_MAX_DEPTH  8
#define SPU_SEU_VARIANTS   16
#define SPU_SEU_TREE_ENTRIES (SPU_SEU_VARIANTS * SPU_SEU_MAX_DEPTH) /* 128 */

/**
 * Configure the SEU prediction tree parameters.
 * @param depth      Tree depth (5..8)
 * @param offset     Branch offsets (packed)
 * @param tree_addr  Base address in vector memory for tree storage
 * @param prob_base  Base address for probability configuration
 * @return 0 on success, -1 on error
 */
int spu_seu_configure(spu_t *h, uint32_t depth, uint32_t offset,
		      uint32_t tree_addr, uint32_t prob_base);

/**
 * Start SEU tree generation (runs in parallel with dot-product).
 * @return 0 on success, -1 on error
 */
int spu_seu_start(spu_t *h);

/**
 * Read the generated prediction tree.
 * @param entries   Output array of 128 float probability scores
 * @param status    Output: SEU status (0=ready, 1=busy, 2=done)
 * @return 0 on success, -1 on error
 */
int spu_seu_get_tree(spu_t *h, float entries[SPU_SEU_TREE_ENTRIES],
		     uint32_t *status);

/**
 * Configure, start, and wait for SEU tree generation.
 * @param depth       Tree depth (5..8)
 * @param offset      Branch offsets
 * @param entries     Output: 128 probability scores
 * @param timeout_ms  0 = wait indefinitely
 * @return 0 on success, -1 on error
 */
int spu_predict_tree(spu_t *h, uint32_t depth, uint32_t offset,
		     float entries[SPU_SEU_TREE_ENTRIES],
		     unsigned timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SPU_H */
