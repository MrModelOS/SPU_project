/*
 * sppu_searchd — Minimal HTTP semantic search server using /dev/sppu
 *
 * Endpoints:
 *   POST /search   {"vector":[0.1,0.2,...], "top_k":5}
 *   POST /load     {"file":"/path/to/vectors.bin"}
 *   GET  /status
 *   GET  /health
 *
 * Usage:
 *   ./sppu_searchd [--port 8080] [--db vectors.bin]
 *
 * Dependencies: libsppu.so, POSIX sockets, fork()
 * No external libraries required.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sppu.h"
#include "sppu_device.h"

#define REQ_BUF   (1 << 20)
#define MAX_DIM   768
#define MAX_TOP_K 100
#define BATCH_SZ  SPPU_MAX_VECTORS

/* ---------- in-memory vector database --------------------------------- */

typedef struct {
	uint32_t count;
	uint32_t dim;
	float   *data;
	pthread_mutex_t lock; /* not really needed since fork model */
} vec_db_t;

static vec_db_t db = {0};

/* ---------- minimal JSON parser --------------------------------------- */

/* Find a JSON key's value start position. Returns pointer after ':' */
static const char *json_find_key(const char *json, const char *key)
{
	char needle[256];
	snprintf(needle, sizeof(needle), "\"%s\"", key);

	const char *p = strstr(json, needle);
	if (!p) return NULL;
	p += strlen(needle);
	while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
	return p;
}

/* Parse a JSON array of numbers into float array. Returns count parsed. */
static int json_parse_float_array(const char *str, float *out, int max_count)
{
	const char *p = str;
	while (*p && *p != '[') p++;
	if (*p != '[') return -1;
	p++;

	int count = 0;
	while (*p && *p != ']' && count < max_count) {
		while (*p && (*p == ' ' || *p == ',' || *p == '\t')) p++;
		if (*p == ']') break;
		char *end;
		float v = strtof(p, &end);
		if (end == p) return -1;
		out[count++] = v;
		p = end;
	}
	return count;
}

/* Parse a JSON integer value */
static int json_parse_int(const char *str, int default_val)
{
	const char *p = str;
	while (*p && (*p == ' ' || *p == '\t')) p++;
	if (*p == '"') {
		p++;
		while (*p && *p != '"') p++;
		return default_val;
	}
	char *end;
	int v = (int)strtol(p, &end, 10);
	if (end == p) return default_val;
	return v;
}

/* Extract a JSON string value (unquoted). Writes to out, returns length. */
static int json_parse_string(const char *str, char *out, int max_len)
{
	const char *p = str;
	while (*p && *p != '"') p++;
	if (*p != '"') return -1;
	p++;
	int len = 0;
	while (*p && *p != '"' && len < max_len - 1) {
		if (*p == '\\') { p++; }
		out[len++] = *p++;
	}
	out[len] = '\0';
	return len;
}

/* ---------- database I/O ---------------------------------------------- */

static int db_load_binary(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -1;

	uint32_t count, dim;
	if (fread(&count, 4, 1, f) != 1 || fread(&dim, 4, 1, f) != 1) {
		fclose(f);
		return -1;
	}
	if (count == 0 || dim == 0 || dim > MAX_DIM) {
		fclose(f);
		return -1;
	}

	float *data = malloc((size_t)count * dim * sizeof(float));
	if (!data) { fclose(f); return -1; }

	size_t n = fread(data, sizeof(float), (size_t)count * dim, f);
	fclose(f);
	if (n != (size_t)count * dim) { free(data); return -1; }

	free(db.data);
	db.count = count;
	db.dim   = dim;
	db.data  = data;
	return 0;
}

static int db_load_csv(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;

	char line[8192];

	/* count lines and detect dimension */
	uint32_t count = 0;
	uint32_t dim = 0;
	while (fgets(line, sizeof(line), f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\n' || *p == '#' || *p == '\r') continue;
		if (dim == 0) {
			const char *q = p;
			int d = 0;
			while (*q && *q != '\n') {
				char *end;
				while (*q && (*q == ';' || *q == ',' || *q == ' ')) q++;
				if (!*q || *q == '\n') break;
				strtof(q, &end);
				if (end == q) break;
				d++;
				q = end;
			}
			dim = (uint32_t)d;
		}
		count++;
	}

	if (dim == 0 || count == 0) { fclose(f); return -1; }

	float *data = malloc((size_t)count * dim * sizeof(float));
	if (!data) { fclose(f); return -1; }

	rewind(f);
	uint32_t row = 0;
	while (fgets(line, sizeof(line), f) && row < count) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\n' || *p == '#') continue;

		for (uint32_t d = 0; d < dim; d++) {
			while (*p && (*p == ';' || *p == ',' || *p == ' ')) p++;
			char *end;
			data[row * dim + d] = strtof(p, &end);
			p = end;
		}
		row++;
	}
	fclose(f);

	free(db.data);
	db.count = count;
	db.dim   = dim;
	db.data  = data;
	return 0;
}

/* ---------- SPPU search ------------------------------------------------ */

static int sppu_search_batch(const float *query, uint32_t dim,
			    uint32_t offset, uint32_t batch_count,
			    uint32_t *best_idx, float *best_score)
{
	sppu_t *sppu = sppu_open(NULL);
	if (!sppu) return -1;

	int rc = -1;
	if (sppu_reset(sppu) == 0 &&
	    sppu_configure(sppu, batch_count, dim) == 0) {
		rc = 0;
		for (uint32_t i = 0; i < batch_count && rc == 0; i++)
			rc = sppu_load_vector(sppu, i,
					     db.data + (size_t)(offset + i) * dim,
					     dim);
		if (rc == 0) rc = sppu_set_target(sppu, query, dim);
		if (rc == 0) rc = sppu_start(sppu);
	}

	if (rc == 0) {
		uint32_t idx = 0, status = 0;
		float score = 0;
		rc = sppu_wait_result(sppu, &idx, &score, &status, 10000);
		if (rc == 0) {
			*best_idx   = offset + idx;
			*best_score = score;
		}
	}

	sppu_close(sppu);
	return rc;
}

/* Simple min-heap for top-K */
typedef struct {
	uint32_t idx;
	float    score;
} heap_entry_t;

static void heap_sift_down(heap_entry_t *h, int n, int i)
{
	while (1) {
		int smallest = i;
		int l = 2 * i + 1, r = 2 * i + 2;
		if (l < n && h[l].score < h[smallest].score) smallest = l;
		if (r < n && h[r].score < h[smallest].score) smallest = r;
		if (smallest == i) break;
		heap_entry_t tmp = h[i]; h[i] = h[smallest]; h[smallest] = tmp;
		i = smallest;
	}
}

static void do_search(const char *body, int client_fd)
{
	char response[4096];

	float *vec = malloc(MAX_DIM * sizeof(float));
	if (!vec) {
		snprintf(response, sizeof(response),
			 "HTTP/1.1 500 Internal Server Error\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "malloc failed\n");
		write(client_fd, response, strlen(response));
		return;
	}

	const char *vec_str = json_find_key(body, "vector");
	if (!vec_str) {
		free(vec);
		snprintf(response, sizeof(response),
			 "HTTP/1.1 400 Bad Request\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "Missing 'vector' field\n");
		write(client_fd, response, strlen(response));
		return;
	}

	int qdim = json_parse_float_array(vec_str, vec, MAX_DIM);
	if (qdim <= 0) {
		free(vec);
		snprintf(response, sizeof(response),
			 "HTTP/1.1 400 Bad Request\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "Invalid vector JSON\n");
		write(client_fd, response, strlen(response));
		return;
	}

	int top_k = 5;
	const char *tk_str = json_find_key(body, "top_k");
	if (tk_str) top_k = json_parse_int(tk_str, 5);
	if (top_k < 1) top_k = 1;
	if (top_k > MAX_TOP_K) top_k = MAX_TOP_K;

	if (db.data == NULL || db.count == 0) {
		free(vec);
		snprintf(response, sizeof(response),
			 "HTTP/1.1 503 Service Unavailable\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "No database loaded. Use POST /load first.\n");
		write(client_fd, response, strlen(response));
		return;
	}

	if ((uint32_t)qdim != db.dim) {
		free(vec);
		snprintf(response, sizeof(response),
			 "HTTP/1.1 400 Bad Request\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "Query dim (%d) != DB dim (%u)\n",
			 qdim, db.dim);
		write(client_fd, response, strlen(response));
		return;
	}

	/* heap for top-K */
	heap_entry_t *heap = malloc(top_k * sizeof(heap_entry_t));
	int heap_n = 0;

	uint32_t batch_start = 0;
	while (batch_start < db.count) {
		uint32_t batch = db.count - batch_start;
		if (batch > BATCH_SZ) batch = BATCH_SZ;

		uint32_t bidx = 0;
		float bscore = 0;
		if (sppu_search_batch(vec, db.dim, batch_start, batch,
				     &bidx, &bscore) < 0) {
			batch_start += batch;
			continue;
		}

		if (heap_n < top_k) {
			heap[heap_n].idx   = bidx;
			heap[heap_n].score = bscore;
			heap_n++;
			for (int i = heap_n / 2 - 1; i >= 0; i--)
				heap_sift_down(heap, heap_n, i);
		} else if (bscore > heap[0].score) {
			heap[0].idx   = bidx;
			heap[0].score = bscore;
			heap_sift_down(heap, heap_n, 0);
		}

		batch_start += batch;
	}

	/* build JSON response */
	char *p = response;
	int rem = sizeof(response);
	int n;

	n = snprintf(p, rem,
		     "HTTP/1.1 200 OK\r\n"
		     "Content-Type: application/json\r\n"
		     "Connection: close\r\n\r\n"
		     "{\"results\":[");
	p += n; rem -= n;

	/* sort heap descending by score for output */
	for (int i = heap_n - 1; i > 0; i--) {
		heap_entry_t tmp = heap[0];
		heap[0] = heap[i];
		heap[i] = tmp;
		heap_sift_down(heap, i, 0);
	}

	for (int i = 0; i < heap_n && rem > 0; i++) {
		n = snprintf(p, rem, "%s{\"index\":%u,\"score\":%.6f}",
			     i > 0 ? "," : "",
			     heap[i].idx, heap[i].score);
		p += n; rem -= n;
	}

	n = snprintf(p, rem, "],\"count\":%d}", heap_n);
	p += n; rem -= n;

	write(client_fd, response, (size_t)(p - response));
	free(heap);
	free(vec);
}

static void do_load(const char *body, int client_fd)
{
	char response[4096];
	char filepath[1024] = {0};

	const char *fstr = json_find_key(body, "file");
	if (!fstr) {
		snprintf(response, sizeof(response),
			 "HTTP/1.1 400 Bad Request\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "Missing 'file' field\n");
		write(client_fd, response, strlen(response));
		return;
	}

	json_parse_string(fstr, filepath, sizeof(filepath));
	if (!filepath[0]) {
		snprintf(response, sizeof(response),
			 "HTTP/1.1 400 Bad Request\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "Invalid file path\n");
		write(client_fd, response, strlen(response));
		return;
	}

	int rc;
	size_t len = strlen(filepath);
	if (len > 4 && strcmp(filepath + len - 4, ".csv") == 0)
		rc = db_load_csv(filepath);
	else
		rc = db_load_binary(filepath);

	if (rc < 0) {
		snprintf(response, sizeof(response),
			 "HTTP/1.1 500 Internal Server Error\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n\r\n"
			 "Failed to load: %s\n", filepath);
		write(client_fd, response, strlen(response));
		return;
	}

	snprintf(response, sizeof(response),
		 "HTTP/1.1 200 OK\r\n"
		 "Content-Type: application/json\r\n"
		 "Connection: close\r\n\r\n"
		 "{\"status\":\"ok\",\"count\":%u,\"dim\":%u}",
		 db.count, db.dim);
	write(client_fd, response, strlen(response));
}

static void do_status(int client_fd)
{
	char response[1024];
	snprintf(response, sizeof(response),
		 "HTTP/1.1 200 OK\r\n"
		 "Content-Type: application/json\r\n"
		 "Connection: close\r\n\r\n"
		 "{\"count\":%u,\"dim\":%u,\"status\":\"%s\"}",
		 db.count, db.dim,
		 db.data ? "ready" : "no_database");
	write(client_fd, response, strlen(response));
}

static void do_health(int client_fd)
{
	const char *resp =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"Connection: close\r\n\r\n"
		"{\"status\":\"ok\"}";
	write(client_fd, resp, strlen(resp));
}

static void do_404(int client_fd)
{
	const char *resp =
		"HTTP/1.1 404 Not Found\r\n"
		"Content-Type: text/plain\r\n"
		"Connection: close\r\n\r\n"
		"Not Found\n";
	write(client_fd, resp, strlen(resp));
}

/* ---------- HTTP request handling ------------------------------------- */

static void handle_request(int client_fd, struct sockaddr_in *client_addr)
{
	char *buf = malloc(REQ_BUF);
	if (!buf) { close(client_fd); return; }

	ssize_t n = read(client_fd, buf, REQ_BUF - 1);
	if (n <= 0) { free(buf); close(client_fd); return; }
	buf[n] = '\0';

	/* parse method and path */
	char method[16] = {0}, path[256] = {0};
	sscanf(buf, "%15s %255s", method, path);

	char *body = strstr(buf, "\r\n\r\n");
	if (body) body += 4;

	char addr_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &client_addr->sin_addr, addr_str, sizeof(addr_str));
	printf("[%s] %s %s\n", addr_str, method, path);

	if (strcmp(method, "GET") == 0) {
		if (strcmp(path, "/health") == 0)
			do_health(client_fd);
		else if (strcmp(path, "/status") == 0)
			do_status(client_fd);
		else
			do_404(client_fd);
	} else if (strcmp(method, "POST") == 0) {
		if (strcmp(path, "/search") == 0)
			do_search(body ? body : "", client_fd);
		else if (strcmp(path, "/load") == 0)
			do_load(body ? body : "", client_fd);
		else
			do_404(client_fd);
	} else {
		do_404(client_fd);
	}

	free(buf);
	close(client_fd);
}

/* ---------- main ------------------------------------------------------ */

static volatile int running = 1;

static void sigint_handler(int sig)
{
	(void)sig;
	running = 0;
}

static void sigchld_handler(int sig)
{
	(void)sig;
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"SPPU Search Daemon — HTTP semantic search\n\n"
		"Usage:\n"
		"  %s [--port PORT] [--db file.bin]\n\n"
		"Endpoints:\n"
		"  POST /search  {\"vector\":[...], \"top_k\":5}\n"
		"  POST /load    {\"file\":\"/path/to/db.bin\"}\n"
		"  GET  /status\n"
		"  GET  /health\n\n"
		"Options:\n"
		"  --port PORT   Listen port (default: 8080)\n"
		"  --db FILE     Load database on startup\n"
		"  -h, --help    This help\n",
		prog);
}

int main(int argc, char **argv)
{
	int port = 8080;
	const char *db_path = NULL;

	static struct option long_opts[] = {
		{ "port", required_argument, NULL, 'p' },
		{ "db",   required_argument, NULL, 'd' },
		{ "help", no_argument,       NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "p:d:h", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'p': port = atoi(optarg); break;
		case 'd': db_path = optarg; break;
		case 'h': usage(argv[0]); return 0;
		default:  usage(argv[0]); return 1;
		}
	}

	signal(SIGINT,  sigint_handler);
	signal(SIGTERM, sigint_handler);
	signal(SIGCHLD, sigchld_handler);

	/* load initial database */
	if (db_path) {
		size_t len = strlen(db_path);
		int is_csv = (len > 4 && strcmp(db_path + len - 4, ".csv") == 0);
		int rc = is_csv ? db_load_csv(db_path) : db_load_binary(db_path);
		if (rc < 0) {
			fprintf(stderr, "Failed to load database: %s\n", db_path);
			return 1;
		}
		printf("Loaded database: %u vectors, dim=%u\n", db.count, db.dim);
	}

	int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (srv_fd < 0) { perror("socket"); return 1; }

	int optval = 1;
	setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons((uint16_t)port);

	if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(srv_fd);
		return 1;
	}
	if (listen(srv_fd, 128) < 0) {
		perror("listen");
		close(srv_fd);
		return 1;
	}

	printf("SPPU Search daemon listening on port %d\n", port);
	printf("Endpoints:\n");
	printf("  POST /search  {\"vector\":[...], \"top_k\":5}\n");
	printf("  POST /load    {\"file\":\"path/to/db.bin\"}\n");
	printf("  GET  /status\n");
	printf("  GET  /health\n");

	while (running) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(srv_fd,
				       (struct sockaddr *)&client_addr,
				       &client_len);
		if (client_fd < 0) {
			if (errno == EINTR) continue;
			perror("accept");
			continue;
		}

		pid_t pid = fork();
		if (pid == 0) {
			close(srv_fd);
			handle_request(client_fd, &client_addr);
			_exit(0);
		} else if (pid > 0) {
			close(client_fd);
		} else {
			perror("fork");
			close(client_fd);
		}
	}

	printf("\nShutting down...\n");
	close(srv_fd);
	free(db.data);

	return 0;
}
