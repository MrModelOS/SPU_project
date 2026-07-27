/*
 * sppu_search — CLI utility for similarity search via /dev/sppu
 *
 * Supports binary (.bin) and CSV vector databases.
 * Processes vectors in batches of SPPU_MAX_VECTORS (1000).
 *
 * Usage:
 *   sppu_search --db vectors.bin --query query.bin --top 5
 *   sppu_search --db vectors.csv --query "0.1;0.2;0.3" --top 3
 *   sppu_search --db vectors.csv --query-file q.csv --top 10
 *   sppu_search --generate 5000 128 --db gen.csv
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <time.h>

#include "sppu.h"
#include "sppu_device.h"

#define MAX_TOP_K  100
#define LINE_BUF   8192
#define BATCH_SIZE SPPU_MAX_VECTORS

typedef struct {
	uint32_t index;
	float    score;
} result_t;

static int cmp_desc(const void *a, const void *b)
{
	float sa = ((const result_t *)a)->score;
	float sb = ((const result_t *)b)->score;
	if (sb > sa) return  1;
	if (sb < sa) return -1;
	return 0;
}

typedef struct {
	uint32_t count;
	uint32_t dim;
	float   *data;
} vec_db_t;

static int load_binary(const char *path, vec_db_t *db)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return -1; }

	if (fread(&db->count, sizeof(uint32_t), 1, f) != 1 ||
	    fread(&db->dim,   sizeof(uint32_t), 1, f) != 1) {
		fprintf(stderr, "%s: failed to read header\n", path);
		fclose(f);
		return -1;
	}

	if (db->count == 0 || db->dim == 0 || db->dim > SPPU_MAX_DIMENSION) {
		fprintf(stderr, "%s: invalid count=%u dim=%u\n",
			path, db->count, db->dim);
		fclose(f);
		return -1;
	}

	db->data = malloc((size_t)db->count * db->dim * sizeof(float));
	if (!db->data) { fclose(f); return -1; }

	size_t expect = (size_t)db->count * db->dim;
	size_t n = fread(db->data, sizeof(float), expect, f);
	if (n != expect) {
		fprintf(stderr, "%s: truncated (got %zu of %zu floats)\n",
			path, n, expect);
		free(db->data);
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int write_binary(const char *path, const vec_db_t *db)
{
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return -1; }
	fwrite(&db->count, sizeof(uint32_t), 1, f);
	fwrite(&db->dim,   sizeof(uint32_t), 1, f);
	fwrite(db->data,   sizeof(float), (size_t)db->count * db->dim, f);
	fclose(f);
	return 0;
}

static int is_delim(char c)
{
	return c == ',' || c == ';' || c == ' ' || c == '\t';
}

static float *parse_csv_line(const char *line, uint32_t dim)
{
	float *vec = malloc(dim * sizeof(float));
	if (!vec) return NULL;
	const char *p = line;
	for (uint32_t d = 0; d < dim; d++) {
		while (*p && is_delim(*p)) p++;
		if (!*p || *p == '\n' || *p == '\r') {
			free(vec);
			return NULL;
		}
		char *end;
		vec[d] = strtof(p, &end);
		if (end == p) { free(vec); return NULL; }
		p = end;
	}
	return vec;
}

static int count_csv_columns(const char *line)
{
	int d = 0;
	const char *p = line;
	while (*p && *p != '\n' && *p != '\r') {
		while (*p && is_delim(*p)) p++;
		if (!*p || *p == '\n' || *p == '\r') break;
		char *end;
		strtof(p, &end);
		if (end == p) break;
		d++;
		p = end;
	}
	return d;
}

static int load_csv(const char *path, vec_db_t *db)
{
	FILE *f = fopen(path, "r");
	if (!f) { perror(path); return -1; }

	uint32_t count = 0;
	uint32_t dim = 0;
	char line[LINE_BUF];

	while (fgets(line, sizeof(line), f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\n' || *p == '\r' || *p == '#') continue;
		if (dim == 0)
			dim = (uint32_t)count_csv_columns(p);
		count++;
	}

	if (dim == 0 || count == 0) {
		fprintf(stderr, "%s: empty or invalid CSV\n", path);
		fclose(f);
		return -1;
	}
	if (dim > SPPU_MAX_DIMENSION) {
		fprintf(stderr, "%s: dimension %u exceeds max %u\n",
			path, dim, SPPU_MAX_DIMENSION);
		fclose(f);
		return -1;
	}

	db->count = count;
	db->dim = dim;
	db->data = malloc((size_t)count * dim * sizeof(float));
	if (!db->data) { fclose(f); return -1; }

	rewind(f);
	uint32_t row = 0;
	while (fgets(line, sizeof(line), f) && row < count) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\n' || *p == '\r' || *p == '#') continue;

		float *vec = parse_csv_line(p, dim);
		if (!vec) {
			fprintf(stderr, "%s: parse error at line %u\n",
				path, row + 1);
			free(db->data);
			fclose(f);
			return -1;
		}
		memcpy(db->data + (size_t)row * dim, vec, dim * sizeof(float));
		free(vec);
		row++;
	}
	fclose(f);
	return 0;
}

static int write_csv(const char *path, const vec_db_t *db)
{
	FILE *f = fopen(path, "w");
	if (!f) { perror(path); return -1; }
	for (uint32_t i = 0; i < db->count; i++) {
		for (uint32_t d = 0; d < db->dim; d++) {
			if (d > 0) fprintf(f, ";");
			fprintf(f, "%.6f", db->data[i * db->dim + d]);
		}
		fprintf(f, "\n");
	}
	fclose(f);
	return 0;
}

static float *parse_inline_query(const char *str, uint32_t *out_dim)
{
	const char *p = str;
	int d = 0;
	const char *q = p;
	while (*q) {
		while (*q && is_delim(*q)) q++;
		if (!*q) break;
		char *end;
		strtof(q, &end);
		if (end == q) break;
		d++;
		q = end;
	}
	if (d == 0) return NULL;

	*out_dim = (uint32_t)d;
	float *vec = malloc(d * sizeof(float));
	if (!vec) return NULL;

	for (int i = 0; i < d; i++) {
		while (*p && is_delim(*p)) p++;
		char *end;
		vec[i] = strtof(p, &end);
		p = end;
	}
	return vec;
}

static int search_batch(sppu_t *sppu, const vec_db_t *db,
			uint32_t offset, uint32_t batch_count,
			const float *query, uint32_t dim,
			uint32_t *best_idx, float *best_score)
{
	if (sppu_reset(sppu) < 0) {
		perror("sppu_reset");
		return -1;
	}
	if (sppu_configure(sppu, batch_count, dim) < 0) {
		perror("sppu_configure");
		return -1;
	}
	for (uint32_t i = 0; i < batch_count; i++) {
		if (sppu_load_vector(sppu, i,
				    db->data + (size_t)(offset + i) * dim,
				    dim) < 0) {
			perror("sppu_load_vector");
			return -1;
		}
	}
	if (sppu_set_target(sppu, query, dim) < 0) {
		perror("sppu_set_target");
		return -1;
	}
	if (sppu_start(sppu) < 0) {
		perror("sppu_start");
		return -1;
	}

	uint32_t idx = 0, status = 0;
	float score = 0.0f;
	if (sppu_wait_result(sppu, &idx, &score, &status, 10000) < 0) {
		perror("sppu_wait_result");
		return -1;
	}
	*best_idx   = offset + idx;
	*best_score = score;
	return 0;
}

static result_t *g_results = NULL;
static uint32_t  g_result_cap = 0;
static uint32_t  g_result_count = 0;

static void results_init(uint32_t cap)
{
	g_results = malloc(cap * sizeof(result_t));
	g_result_cap = cap;
	g_result_count = 0;
}

static void add_result(uint32_t global_idx, float score)
{
	if (g_result_count < g_result_cap) {
		g_results[g_result_count].index = global_idx;
		g_results[g_result_count].score = score;
		g_result_count++;
	} else {
		qsort(g_results, g_result_count, sizeof(result_t), cmp_desc);
		if (score > g_results[g_result_count - 1].score) {
			g_results[g_result_count - 1].index = global_idx;
			g_results[g_result_count - 1].score = score;
		}
	}
}

static void print_results(uint32_t top_k, const vec_db_t *db)
{
	qsort(g_results, g_result_count, sizeof(result_t), cmp_desc);
	if (top_k > g_result_count)
		top_k = g_result_count;

	printf("\n=== Top %u results ===\n", top_k);
	printf("%-10s  %-14s\n", "Index", "Score (dot)");
	printf("----------  --------------\n");

	for (uint32_t i = 0; i < top_k; i++) {
		uint32_t idx = g_results[i].index;
		printf("%-10u  %-14.6f", idx, g_results[i].score);
		if (db && db->dim <= 8) {
			printf("  [");
			for (uint32_t d = 0; d < db->dim; d++) {
				if (d > 0) printf(", ");
				printf("%.3f", db->data[(size_t)idx * db->dim + d]);
			}
			printf("]");
		}
		printf("\n");
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"SPPU Search - similarity search via /dev/sppu\n\n"
		"Usage:\n"
		"  %s --db <file> --query <vector|file> [--top K]\n"
		"  %s --db <file> --query-file <file> [--top K]\n"
		"  %s --generate <n> <dim> --db <output>\n\n"
		"Options:\n"
		"  --db <file>        Database file (.bin or .csv)\n"
		"  --query <vector>   Inline query: \"0.1;0.2;0.3\"\n"
		"  --query-file <f>   Query vector from file (.bin or .csv)\n"
		"  --top K            Number of top results (default: 5)\n"
		"  --format bin|csv   Force format (auto-detect by extension)\n"
		"  --generate n d     Generate random DB: n vectors, dim d\n"
		"  -h, --help         This help\n\n"
		"Examples:\n"
		"  %s --db emb.bin --query query.bin --top 5\n"
		"  %s --db emb.csv --query \"0.1;0.2;0.3\" --top 3\n"
		"  %s --generate 5000 128 --db gen.csv\n",
		prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
	const char *db_path    = NULL;
	const char *query_str  = NULL;
	const char *query_file = NULL;
	const char *format     = NULL;
	uint32_t    top_k      = 5;
	int         gen_count  = 0;
	int         gen_dim    = 0;

	static struct option long_opts[] = {
		{ "db",         required_argument, NULL, 'd' },
		{ "query",      required_argument, NULL, 'q' },
		{ "query-file", required_argument, NULL, 'Q' },
		{ "top",        required_argument, NULL, 'k' },
		{ "format",     required_argument, NULL, 'f' },
		{ "generate",   no_argument,       NULL, 'g' },
		{ "help",       no_argument,       NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "d:q:Q:k:f:gh", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'd': db_path    = optarg; break;
		case 'q': query_str  = optarg; break;
		case 'Q': query_file = optarg; break;
		case 'k': top_k      = (uint32_t)atoi(optarg); break;
		case 'f': format     = optarg; break;
		case 'g':
			if (optind + 1 < argc) {
				gen_count = atoi(argv[optind]);
				gen_dim   = atoi(argv[optind + 1]);
				optind += 2;
			}
			break;
		case 'h': usage(argv[0]); return 0;
		default:  usage(argv[0]); return 1;
		}
	}

	if (top_k == 0 || top_k > MAX_TOP_K)
		top_k = 5;

	/* --- generate mode --- */
	if (gen_count > 0 && gen_dim > 0) {
		if (!db_path) {
			fprintf(stderr, "Error: --generate requires --db <output>\n");
			return 1;
		}
		if ((uint32_t)gen_dim > SPPU_MAX_DIMENSION) {
			fprintf(stderr, "Error: dim %d exceeds max %u\n",
				gen_dim, SPPU_MAX_DIMENSION);
			return 1;
		}
		vec_db_t gen;
		gen.count = (uint32_t)gen_count;
		gen.dim   = (uint32_t)gen_dim;
		gen.data  = malloc((size_t)gen.count * gen.dim * sizeof(float));
		if (!gen.data) { perror("malloc"); return 1; }

		srand((unsigned)time(NULL));
		for (uint32_t i = 0; i < gen.count * gen.dim; i++)
			gen.data[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;

		size_t len = strlen(db_path);
		if (len > 4 && strcmp(db_path + len - 4, ".csv") == 0)
			write_csv(db_path, &gen);
		else
			write_binary(db_path, &gen);

		printf("Generated %u vectors of dim %u -> %s\n",
		       gen.count, gen.dim, db_path);
		free(gen.data);
		return 0;
	}

	/* --- search mode --- */
	if (!db_path) {
		fprintf(stderr, "Error: --db is required\n");
		usage(argv[0]);
		return 1;
	}
	if (!query_str && !query_file) {
		fprintf(stderr, "Error: --query or --query-file is required\n");
		usage(argv[0]);
		return 1;
	}

	vec_db_t db = {0};
	int is_csv = 0;
	if (format) {
		is_csv = (strcmp(format, "csv") == 0);
	} else {
		size_t len = strlen(db_path);
		is_csv = (len > 4 && strcmp(db_path + len - 4, ".csv") == 0);
	}

	int rc = is_csv ? load_csv(db_path, &db) : load_binary(db_path, &db);
	if (rc < 0) return 1;

	printf("Loaded database: %u vectors, dim=%u (%s)\n",
	       db.count, db.dim, is_csv ? "CSV" : "binary");

	float *query = NULL;
	uint32_t query_dim = 0;

	if (query_file) {
		vec_db_t qdb = {0};
		size_t len = strlen(query_file);
		int qcsv = (len > 4 && strcmp(query_file + len - 4, ".csv") == 0);
		if (format) qcsv = (strcmp(format, "csv") == 0);

		rc = qcsv ? load_csv(query_file, &qdb) : load_binary(query_file, &qdb);
		if (rc < 0) { free(db.data); return 1; }

		if (qdb.count < 1) {
			fprintf(stderr, "Query file is empty\n");
			free(qdb.data); free(db.data);
			return 1;
		}
		query_dim = qdb.dim;
		query = malloc(query_dim * sizeof(float));
		memcpy(query, qdb.data, query_dim * sizeof(float));
		free(qdb.data);
	} else {
		query = parse_inline_query(query_str, &query_dim);
		if (!query) {
			fprintf(stderr, "Failed to parse query vector: %s\n",
				query_str);
			free(db.data);
			return 1;
		}
	}

	if (query_dim != db.dim) {
		fprintf(stderr, "Query dim (%u) != database dim (%u)\n",
			query_dim, db.dim);
		free(query); free(db.data);
		return 1;
	}

	printf("Query dimension: %u\n", query_dim);

	sppu_t *sppu = sppu_open(NULL);
	if (!sppu) {
		perror("sppu_open (is kernel module loaded?)");
		free(query); free(db.data);
		return 1;
	}

	results_init(top_k * 2);

	uint32_t total = db.count;
	uint32_t batch = 0;
	uint32_t batch_start = 0;

	printf("Searching %u vectors in batches of %u...\n", total, BATCH_SIZE);

	while (batch_start < total) {
		batch = total - batch_start;
		if (batch > BATCH_SIZE)
			batch = BATCH_SIZE;

		uint32_t best_idx = 0;
		float best_score = 0.0f;

		fprintf(stderr, "  batch %u..%u (%u vectors) ",
			batch_start, batch_start + batch - 1, batch);

		rc = search_batch(sppu, &db, batch_start, batch,
				  query, query_dim, &best_idx, &best_score);
		if (rc < 0) {
			fprintf(stderr, "FAILED\n");
			sppu_close(sppu);
			free(query); free(db.data); free(g_results);
			return 1;
		}

		fprintf(stderr, "best=%u score=%.6f\n", best_idx, best_score);
		add_result(best_idx, best_score);

		batch_start += batch;
	}

	print_results(top_k, &db);

	sppu_close(sppu);
	free(query);
	free(db.data);
	free(g_results);
	return 0;
}
