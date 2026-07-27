/**
 * sppu_backend — Backend inference integration for speculative tree validation
 *
 * Connects the SPPU/SEU speculative tree output with a GPU inference backend
 * (llama.cpp, custom runtime, etc.) for parallel forward pass validation.
 *
 * Workflow:
 *   1. sppu_seu_build_tree() — generate speculative branches on FPGA
 *   2. sppu_backend_submit_hypotheses() — send branches to GPU for validation
 *   3. sppu_backend_collect_results() — get GPU validation verdicts
 *   4. sppu_backend_accept() or sppu_backend_rollback() — commit or revert
 */

#ifndef SPPU_BACKEND_H
#define SPPU_BACKEND_H

#include "sppu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hypothesis batch submitted for parallel forward pass on GPU.
 * Each hypothesis is a continuation branch from the speculative tree.
 */
struct sppu_hypothesis {
	uint32_t branch_id;		/* Branch index (0..15)                */
	uint32_t depth;			/* Depth level in tree                 */
	float    tokens[8];		/* Token sequence for this hypothesis  */
	uint32_t token_count;		/* Number of valid tokens              */
	float    score;			/* Hardware confidence score           */
};

/**
 * Validation result from GPU forward pass.
 */
struct sppu_validation_result {
	uint32_t branch_id;		/* Branch index                        */
	uint32_t accepted;		/* 1 = accepted, 0 = rejected          */
	float    gpu_score;		/* GPU-generated logit/score           */
	float    ppl;			/* Perplexity of this continuation     */
};

/**
 * Context for a complete speculative inference session.
 */
struct sppu_speculative_ctx {
	sppu_t   *sppu;			/* SPPU device handle                   */
	uint32_t num_branches;		/* Active branches (1..16)             */
	uint32_t tree_depth;		/* Tree depth (5..8)                   */
	uint32_t context_len;		/* Context token count                 */

	/* Branch tracking */
	struct sppu_hypothesis   hypotheses[16];
	struct sppu_validation_result results[16];
	uint32_t num_submitted;
	uint32_t num_validated;
};

/**
 * Initialize a speculative inference context.
 * @param ctx       Context to initialize
 * @param sppu       Open SPPU device handle
 * @return 0 on success, -1 on error
 */
int sppu_speculative_init(struct sppu_speculative_ctx *ctx, sppu_t *sppu);

/**
 * Build speculative tree and prepare hypotheses for GPU validation.
 * @param ctx            Context
 * @param context_tokens Input context tokens
 * @param token_count    Number of context tokens
 * @param max_branches   Max branches to explore
 * @param tree_depth     Tree depth (5..8)
 * @param node_base      Base address for node weights in vmem
 * @param branch_mask    Active branch mask
 * @return 0 on success, -1 on error
 */
int sppu_speculative_build(struct sppu_speculative_ctx *ctx,
			  const float *context_tokens, uint32_t token_count,
			  uint32_t max_branches, uint32_t tree_depth,
			  uint32_t node_base, uint16_t branch_mask);

/**
 * Submit hypotheses for parallel forward pass on GPU.
 * The caller should pass these to the inference engine (llama.cpp etc.)
 *
 * @param ctx            Context
 * @param num_hypotheses Output: number of hypotheses to validate
 * @param hypotheses     Output: array of hypothesis structs
 * @return 0 on success, -1 on error
 */
int sppu_speculative_submit(struct sppu_speculative_ctx *ctx,
			   uint32_t *num_hypotheses,
			   struct sppu_hypothesis **hypotheses);

/**
 * Accept validated results and commit branches.
 * @param ctx      Context
 * @param results  Array of validation results from GPU
 * @param count    Number of results
 * @return 0 on success, -1 on error
 */
int sppu_speculative_accept(struct sppu_speculative_ctx *ctx,
			   const struct sppu_validation_result *results,
			   uint32_t count);

/**
 * Rollback rejected branches.
 * @param ctx Context
 * @return 0 on success, -1 on error
 */
int sppu_speculative_rollback(struct sppu_speculative_ctx *ctx);

/**
 * Cleanup context.
 * @param ctx Context
 */
void sppu_speculative_cleanup(struct sppu_speculative_ctx *ctx);

/**
 * High-level: run full speculative inference cycle.
 * 1. Build tree  2. Submit  3. Validate callback  4. Accept/rollback
 *
 * @param ctx            Context
 * @param context_tokens Input context
 * @param token_count    Token count
 * @param validate_fn    Callback: called with hypotheses, returns results
 * @param user_data      Opaque pointer for callback
 * @return 0 on success, -1 on error
 */
typedef int (*sppu_validate_fn)(const struct sppu_hypothesis *hyps,
			       uint32_t count,
			       struct sppu_validation_result *results,
			       void *user_data);

int sppu_speculative_run(struct sppu_speculative_ctx *ctx,
			const float *context_tokens, uint32_t token_count,
			sppu_validate_fn validate_fn, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* SPPU_BACKEND_H */
