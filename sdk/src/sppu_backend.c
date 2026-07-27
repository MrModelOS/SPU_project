/*
 * sppu_backend — Backend inference integration implementation
 * Connects speculative tree hardware with GPU inference for validation.
 */

#define _DEFAULT_SOURCE

#include "sppu_backend.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int sppu_speculative_init(struct sppu_speculative_ctx *ctx, sppu_t *sppu)
{
	if (!ctx || !sppu) {
		errno = EINVAL;
		return -1;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->sppu = sppu;
	return 0;
}

int sppu_speculative_build(struct sppu_speculative_ctx *ctx,
			  const float *context_tokens, uint32_t token_count,
			  uint32_t max_branches, uint32_t tree_depth,
			  uint32_t node_base, uint16_t branch_mask)
{
	uint32_t i;
	uint16_t mask;

	if (!ctx || !ctx->sppu) {
		errno = EINVAL;
		return -1;
	}

	ctx->num_branches = max_branches;
	ctx->tree_depth   = tree_depth;
	ctx->context_len  = token_count;

	/* Load context tokens into SPPU vector memory */
	for (i = 0; i < token_count && i < 8; i++) {
		float token_vec[1] = { context_tokens[i] };
		if (sppu_load_vector(ctx->sppu, i, token_vec, 1) < 0)
			return -1;
	}

	/* Build the speculative tree on FPGA */
	mask = branch_mask ? branch_mask : (uint16_t)((1U << max_branches) - 1);

	if (sppu_seu_build_tree(ctx->sppu,
			       0x10000,   /* context_addr in vmem */
			       0x20000,   /* embed_addr in vmem   */
			       max_branches,
			       token_count,
			       1,         /* auto_validate */
			       node_base,
			       mask) < 0)
		return -1;

	/* Wait for tree build to complete */
	for (i = 0; i < 1000; i++) {
		uint16_t valid;
		uint32_t best_idx, entries;
		float best_score;

		if (sppu_seu_get_branches(ctx->sppu, &valid, &best_idx,
					 &best_score, &entries) < 0)
			return -1;

		if (entries > 0) {
			/* Tree built — extract hypotheses */
			ctx->num_submitted = 0;
			for (uint32_t b = 0; b < max_branches; b++) {
				if (!(valid & (1U << b)))
					continue;

				ctx->hypotheses[ctx->num_submitted].branch_id = b;
				ctx->hypotheses[ctx->num_submitted].depth = tree_depth;
				ctx->hypotheses[ctx->num_submitted].token_count = token_count;
				memcpy(ctx->hypotheses[ctx->num_submitted].tokens,
				       context_tokens,
				       token_count * sizeof(float));
				ctx->hypotheses[ctx->num_submitted].score = best_score;
				ctx->num_submitted++;
			}
			return 0;
		}
		usleep(100);
	}

	errno = ETIMEDOUT;
	return -1;
}

int sppu_speculative_submit(struct sppu_speculative_ctx *ctx,
			   uint32_t *num_hypotheses,
			   struct sppu_hypothesis **hypotheses)
{
	if (!ctx || !num_hypotheses || !hypotheses) {
		errno = EINVAL;
		return -1;
	}

	*num_hypotheses = ctx->num_submitted;
	*hypotheses     = ctx->hypotheses;
	return 0;
}

int sppu_speculative_accept(struct sppu_speculative_ctx *ctx,
			   const struct sppu_validation_result *results,
			   uint32_t count)
{
	if (!ctx || !ctx->sppu) {
		errno = EINVAL;
		return -1;
	}

	/* Store results */
	uint32_t accepted_count = 0;
	for (uint32_t i = 0; i < count && i < 16; i++) {
		ctx->results[i] = results[i];
		if (results[i].accepted)
			accepted_count++;
	}
	ctx->num_validated = count;

	if (accepted_count > 0)
		return sppu_seu_accept(ctx->sppu);
	else
		return sppu_seu_rollback(ctx->sppu);
}

int sppu_speculative_rollback(struct sppu_speculative_ctx *ctx)
{
	if (!ctx || !ctx->sppu) {
		errno = EINVAL;
		return -1;
	}
	ctx->num_submitted = 0;
	ctx->num_validated = 0;
	return sppu_seu_rollback(ctx->sppu);
}

void sppu_speculative_cleanup(struct sppu_speculative_ctx *ctx)
{
	if (ctx) {
		ctx->num_submitted = 0;
		ctx->num_validated = 0;
	}
}

int sppu_speculative_run(struct sppu_speculative_ctx *ctx,
			const float *context_tokens, uint32_t token_count,
			sppu_validate_fn validate_fn, void *user_data)
{
	uint32_t num_hyps;
	struct sppu_hypothesis *hyps;

	if (!ctx || !validate_fn) {
		errno = EINVAL;
		return -1;
	}

	/* Step 1: Build speculative tree */
	if (sppu_speculative_build(ctx, context_tokens, token_count,
				  ctx->num_branches ? ctx->num_branches : 16,
				  ctx->tree_depth ? ctx->tree_depth : 6,
				  0, 0) < 0)
		return -1;

	/* Step 2: Get hypotheses */
	if (sppu_speculative_submit(ctx, &num_hyps, &hyps) < 0)
		return -1;

	if (num_hyps == 0) {
		sppu_seu_rollback(ctx->sppu);
		return 0;
	}

	/* Step 3: Call user's validation function (runs on GPU) */
	struct sppu_validation_result *results = calloc(num_hyps, sizeof(*results));
	if (!results) {
		sppu_seu_rollback(ctx->sppu);
		return -1;
	}

	int ret = validate_fn(hyps, num_hyps, results, user_data);
	if (ret < 0) {
		free(results);
		sppu_seu_rollback(ctx->sppu);
		return -1;
	}

	/* Step 4: Accept or rollback based on validation */
	ret = sppu_speculative_accept(ctx, results, num_hyps);
	free(results);
	return ret;
}
