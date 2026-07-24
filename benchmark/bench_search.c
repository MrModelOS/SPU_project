/*
 * bench_search.c — Performance benchmark for dot-product similarity search
 *
 * Tests scalar vs SIMD implementations at various scales.
 * Measures: throughput (vecs/sec), latency (us), speedup vs scalar.
 *
 * Usage:
 *   ./bench_search [--vectors N] [--dim D] [--repeats R]
 *   ./bench_search --full   (run all configurations)
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <math.h>

#include "bench_simd.h"

static double time_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void fill_random(float *data, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
		data[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
}

typedef struct {
	dot_method_t method;
	uint32_t     vec_count;
	uint32_t     dim;
	uint32_t     repeats;
	double       total_ms;
	double       avg_ms;
	double       vecs_per_sec;
	float        ref_score;
	int          correct;
} bench_result_t;

static bench_result_t run_bench(dot_method_t method,
				const float *db, const float *query,
				uint32_t vec_count, uint32_t dim,
				uint32_t repeats, float ref_score)
{
	bench_result_t r;
	memset(&r, 0, sizeof(r));
	r.method   = method;
	r.vec_count = vec_count;
	r.dim      = dim;
	r.repeats  = repeats;

	dot_fn fn = get_dot_fn(method);
	if (!fn) {
		r.avg_ms = -1;
		return r;
	}

	/* warmup */
	for (uint32_t w = 0; w < 3; w++) {
		for (uint32_t i = 0; i < vec_count; i++)
			fn(db + (size_t)i * dim, query, dim);
	}

	double t0 = time_ms();
	for (uint32_t rep = 0; rep < repeats; rep++) {
		for (uint32_t i = 0; i < vec_count; i++)
			fn(db + (size_t)i * dim, query, dim);
	}
	double t1 = time_ms();

	r.total_ms    = t1 - t0;
	r.avg_ms      = r.total_ms / repeats;
	r.vecs_per_sec = (vec_count * repeats) / (r.total_ms / 1000.0);

	/* validate against scalar result */
	float score = -1e30f;
	for (uint32_t i = 0; i < vec_count; i++) {
		float s = fn(db + (size_t)i * dim, query, dim);
		if (s > score) score = s;
	}
	r.ref_score = score;
	r.correct = (fabsf(score - ref_score) < 0.01f);

	return r;
}

static void print_header(void)
{
	printf("%-10s  %8s  %6s  %10s  %12s  %10s  %s\n",
	       "Method", "Vectors", "Dim", "Avg (ms)", "Vecs/sec",
	       "Speedup", "Valid");
	printf("----------  --------  ------  ----------  ------------  ----------  -----\n");
}

static void print_row(const bench_result_t *r, double scalar_ms)
{
	double speedup = (r->avg_ms > 0 && scalar_ms > 0) ?
			  scalar_ms / r->avg_ms : 0;

	printf("%-10s  %8u  %6u  %10.3f  %12.0f  %9.2fx  %s\n",
	       dot_method_name(r->method),
	       r->vec_count,
	       r->dim,
	       r->avg_ms,
	       r->vecs_per_sec,
	       speedup,
	       r->correct ? "PASS" : "FAIL");
}

static void run_single_config(uint32_t vec_count, uint32_t dim,
			      uint32_t repeats)
{
	printf("\n>>> Configuration: %u vectors, dim=%u, %u repeats\n",
	       vec_count, dim, repeats);

	uint32_t db_size = vec_count * dim;
	float *db    = malloc((size_t)db_size * sizeof(float));
	float *query = malloc(dim * sizeof(float));

	srand(42);
	fill_random(db, db_size);
	fill_random(query, dim);

	/* scalar reference */
	double t0 = time_ms();
	float ref_score = -1e30f;
	for (uint32_t i = 0; i < vec_count; i++) {
		float s = dot_scalar(db + (size_t)i * dim, query, dim);
		if (s > ref_score) ref_score = s;
	}
	double t1 = time_ms();
	double scalar_ms = (t1 - t0) / repeats;

	print_header();

	dot_method_t methods[] = { DOT_SCALAR, DOT_SSE42, DOT_AVX2, DOT_AVX512 };
	for (int m = 0; m < DOT_COUNT; m++) {
		if (!dot_method_available(methods[m]))
			continue;
		bench_result_t r = run_bench(methods[m], db, query,
					    vec_count, dim, repeats,
					    ref_score);
		print_row(&r, scalar_ms);
	}

	free(db);
	free(query);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"SPU Benchmark — dot-product search performance\n\n"
		"Usage:\n"
		"  %s [--vectors N] [--dim D] [--repeats R]\n"
		"  %s --full\n\n"
		"Options:\n"
		"  --vectors N   Number of vectors (default: 10000)\n"
		"  --dim D       Vector dimension (default: 128)\n"
		"  --repeats R   Repeat count (default: 10)\n"
		"  --full        Run all configurations\n"
		"  -h, --help    This help\n",
		prog, prog);
}

int main(int argc, char **argv)
{
	uint32_t vec_count = 10000;
	uint32_t dim       = 128;
	uint32_t repeats   = 10;
	int      full      = 0;

	static struct option long_opts[] = {
		{ "vectors", required_argument, NULL, 'n' },
		{ "dim",     required_argument, NULL, 'd' },
		{ "repeats", required_argument, NULL, 'r' },
		{ "full",    no_argument,       NULL, 'f' },
		{ "help",    no_argument,       NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "n:d:r:fh", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'n': vec_count = (uint32_t)atoi(optarg); break;
		case 'd': dim       = (uint32_t)atoi(optarg); break;
		case 'r': repeats   = (uint32_t)atoi(optarg); break;
		case 'f': full = 1; break;
		case 'h': usage(argv[0]); return 0;
		default:  usage(argv[0]); return 1;
		}
	}

	printf("=== SPU Dot-Product Search Benchmark ===\n");
	printf("Compiler: %s\n",
#ifdef __GNUC__
		__VERSION__
#else
		"unknown"
#endif
	);

	char simd_str[128] = {0};
#ifdef __AVX512F__
	strcat(simd_str, "AVX-512 ");
#endif
#ifdef __AVX2__
	strcat(simd_str, "AVX2 ");
#endif
#ifdef __SSE4_2__
	strcat(simd_str, "SSE4.2 ");
#endif
	if (simd_str[0] == '\0')
		strcpy(simd_str, "none");
	else
		simd_str[strlen(simd_str) - 1] = '\0';
	printf("SIMD: %s\n", simd_str);

	if (full) {
		uint32_t counts[] = { 1000, 10000, 50000, 100000 };
		uint32_t dims[]   = { 64, 128, 256, 512, 768 };
		uint32_t reps[]   = { 10, 5, 2, 1 };

		for (int c = 0; c < 4; c++) {
			for (int d = 0; d < 5; d++) {
				uint32_t r = (counts[c] >= 100000) ? 2 : reps[c];
				run_single_config(counts[c], dims[d], r);
			}
		}
	} else {
		run_single_config(vec_count, dim, repeats);
	}

	printf("\n=== Benchmark complete ===\n");
	return 0;
}
