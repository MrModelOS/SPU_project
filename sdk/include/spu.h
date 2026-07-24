/*
 * libspu — userspace SDK for /dev/spu
 *
 * Thin C wrapper over the SPU ioctl ABI defined in spu_device.h.
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

#ifdef __cplusplus
}
#endif

#endif /* SPU_H */
