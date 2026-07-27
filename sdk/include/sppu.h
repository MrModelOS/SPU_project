/**
 * libsppu — userspace SDK for /dev/sppu
 *
 * Thin C wrapper over the SPPU/SEU ioctl ABI defined in sppu_device.h.
 */

#ifndef SPPU_H
#define SPPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sppu_handle sppu_t;

/**
 * Open an SPPU device.
 * @param path  Device path, or NULL for "/dev/sppu"
 * @return handle on success, NULL on error (errno set)
 */
sppu_t *sppu_open(const char *path);

/** Close handle and free resources. Safe with NULL. */
void sppu_close(sppu_t *h);

int sppu_reset(sppu_t *h);
int sppu_configure(sppu_t *h, uint32_t vec_count, uint32_t dimension);
int sppu_load_vector(sppu_t *h, uint32_t index, const float *data, uint32_t dim);
int sppu_set_target(sppu_t *h, const float *data, uint32_t dim);
int sppu_start(sppu_t *h);
int sppu_get_status(sppu_t *h, uint32_t *status);
int sppu_get_result(sppu_t *h, uint32_t *index, float *score, uint32_t *status);

/**
 * Poll STATUS until DONE (or timeout), then read the result.
 * @param timeout_ms  0 means wait indefinitely
 * @return 0 on success, -1 on error (errno: ETIMEDOUT on timeout)
 */
int sppu_wait_result(sppu_t *h, uint32_t *index, float *score,
		    uint32_t *status, unsigned timeout_ms);

/* ========================================================================
 * SEU (Speculative Execution Unit) — Prediction Tree API
 * ======================================================================== */

#ifndef SPPU_SEU_MIN_DEPTH
#define SPPU_SEU_MIN_DEPTH  5
#endif
#ifndef SPPU_SEU_MAX_DEPTH
#define SPPU_SEU_MAX_DEPTH  8
#endif
#ifndef SPPU_SEU_VARIANTS
#define SPPU_SEU_VARIANTS   16
#endif
#ifndef SPPU_SEU_TREE_ENTRIES
#define SPPU_SEU_TREE_ENTRIES (SPPU_SEU_VARIANTS * SPPU_SEU_MAX_DEPTH) /* 128 */
#endif

/**
 * Configure the SEU prediction tree parameters.
 * @param depth      Tree depth (5..8)
 * @param offset     Branch offsets (packed)
 * @param tree_addr  Base address in vector memory for tree storage
 * @param prob_base  Base address for probability configuration
 * @return 0 on success, -1 on error
 */
int sppu_seu_configure(sppu_t *h, uint32_t depth, uint32_t offset,
		      uint32_t tree_addr, uint32_t prob_base);

/**
 * Start SEU tree generation (runs in parallel with dot-product).
 * @return 0 on success, -1 on error
 */
int sppu_seu_start(sppu_t *h);

/**
 * Read the generated prediction tree.
 * @param entries   Output array of 128 float probability scores
 * @param status    Output: SEU status (0=ready, 1=busy, 2=done)
 * @return 0 on success, -1 on error
 */
int sppu_seu_get_tree(sppu_t *h, float entries[SPPU_SEU_TREE_ENTRIES],
		     uint32_t *status);

/**
 * Configure, start, and wait for SEU tree generation.
 * @param depth       Tree depth (5..8)
 * @param offset      Branch offsets
 * @param entries     Output: 128 probability scores
 * @param timeout_ms  0 = wait indefinitely
 * @return 0 on success, -1 on error
 */
int sppu_predict_tree(sppu_t *h, uint32_t depth, uint32_t offset,
		     float entries[SPPU_SEU_TREE_ENTRIES],
		     unsigned timeout_ms);

/* ========================================================================
 * SEU v0.4 — Speculative Tree Walker API
 * ======================================================================== */

/**
 * Build a speculative tree with context tokens.
 * @param context_addr  Address of context tokens in vector memory
 * @param embed_addr    Address of embedding search target
 * @param max_branches  Number of branches to explore (1..16)
 * @param context_len   Number of context tokens (1..8)
 * @param auto_validate If nonzero, auto-validate via embedding search
 * @param node_base     Base address for tree node weights
 * @param branch_mask   Bitmask of active branches (16-bit)
 * @return 0 on success, -1 on error
 */
int sppu_seu_build_tree(sppu_t *h, uint32_t context_addr, uint32_t embed_addr,
		       uint32_t max_branches, uint32_t context_len,
		       uint32_t auto_validate, uint32_t node_base,
		       uint16_t branch_mask);

/**
 * Get the result of speculative tree building.
 * @param branch_valid   Output: 16-bit mask of completed branches
 * @param best_idx       Output: index of best-scoring branch
 * @param best_score     Output: score of best branch
 * @param tree_entries   Output: total tree entries written
 * @return 0 on success, -1 on error
 */
int sppu_seu_get_branches(sppu_t *h, uint16_t *branch_valid,
			 uint32_t *best_idx, float *best_score,
			 uint32_t *tree_entries);

/**
 * Accept the current speculative tree (no rollback).
 * @return 0 on success, -1 on error
 */
int sppu_seu_accept(sppu_t *h);

/**
 * Rollback the current speculative tree (invalidate branches).
 * @return 0 on success, -1 on error
 */
int sppu_seu_rollback(sppu_t *h);

#ifdef __cplusplus
}
#endif

#endif /* SPPU_H */
