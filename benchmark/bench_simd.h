/*
 * bench_simd.h — SIMD-optimized dot product implementations
 *
 * Supports: scalar, SSE4.2, AVX2, AVX-512
 * Auto-detected at compile time via preprocessor macros.
 */

#ifndef BENCH_SIMD_H
#define BENCH_SIMD_H

#include <stdint.h>
#include <math.h>

#ifdef __SSE4_2__
#include <smmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

/* --- scalar fallback --- */
static inline float dot_scalar(const float *a, const float *b, uint32_t dim)
{
	float sum = 0.0f;
	uint32_t i;
	for (i = 0; i < dim; i++)
		sum += a[i] * b[i];
	return sum;
}

/* --- SSE4.2 (128-bit, 4 floats at a time) --- */
#ifdef __SSE4_2__
static inline float dot_sse42(const float *a, const float *b, uint32_t dim)
{
	__m128 sum = _mm_setzero_ps();
	uint32_t i = 0;

	for (; i + 4 <= dim; i += 4) {
		__m128 va = _mm_loadu_ps(a + i);
		__m128 vb = _mm_loadu_ps(b + i);
		sum = _mm_add_ps(sum, _mm_mul_ps(va, vb));
	}

	/* horizontal sum */
	__m128 shuf = _mm_movehdup_ps(sum);
	__m128 sums  = _mm_add_ps(sum, shuf);
	shuf = _mm_movehl_ps(shuf, sums);
	sums = _mm_add_ss(sums, shuf);
	float result = _mm_cvtss_f32(sums);

	/* tail */
	for (; i < dim; i++)
		result += a[i] * b[i];

	return result;
}
#endif

/* --- AVX2 (256-bit, 8 floats at a time) --- */
#ifdef __AVX2__
static inline float dot_avx2(const float *a, const float *b, uint32_t dim)
{
	__m256 sum = _mm256_setzero_ps();
	uint32_t i = 0;

	for (; i + 8 <= dim; i += 8) {
		__m256 va = _mm256_loadu_ps(a + i);
		__m256 vb = _mm256_loadu_ps(b + i);
		sum = _mm256_fmadd_ps(va, vb, sum);
	}

	/* horizontal sum of 256-bit */
	__m128 hi = _mm256_extractf128_ps(sum, 1);
	__m128 lo = _mm256_castps256_ps128(sum);
	__m128 s  = _mm_add_ps(hi, lo);

	__m128 shuf = _mm_movehdup_ps(s);
	__m128 sums  = _mm_add_ps(s, shuf);
	shuf = _mm_movehl_ps(shuf, sums);
	sums = _mm_add_ss(sums, shuf);
	float result = _mm_cvtss_f32(sums);

	for (; i < dim; i++)
		result += a[i] * b[i];

	return result;
}
#endif

/* --- AVX-512 (512-bit, 16 floats at a time) --- */
#ifdef __AVX512F__
static inline float dot_avx512(const float *a, const float *b, uint32_t dim)
{
	__m512 sum = _mm512_setzero_ps();
	uint32_t i = 0;

	for (; i + 16 <= dim; i += 16) {
		__m512 va = _mm512_loadu_ps(a + i);
		__m512 vb = _mm512_loadu_ps(b + i);
		sum = _mm512_fmadd_ps(va, vb, sum);
	}

	float result = _mm512_reduce_add_ps(sum);

	for (; i < dim; i++)
		result += a[i] * b[i];

	return result;
}
#endif

/* --- CPU feature detection --- */
typedef enum {
	DOT_SCALAR = 0,
	DOT_SSE42,
	DOT_AVX2,
	DOT_AVX512,
	DOT_COUNT
} dot_method_t;

static const char *dot_method_name(dot_method_t m)
{
	switch (m) {
	case DOT_SCALAR: return "scalar";
	case DOT_SSE42:  return "SSE4.2";
	case DOT_AVX2:   return "AVX2";
	case DOT_AVX512: return "AVX-512";
	default:         return "unknown";
	}
}

static int dot_method_available(dot_method_t m)
{
	switch (m) {
	case DOT_SCALAR: return 1;
	case DOT_SSE42:
#ifdef __SSE4_2__
		return 1;
#else
		return 0;
#endif
	case DOT_AVX2:
#ifdef __AVX2__
		return 1;
#else
		return 0;
#endif
	case DOT_AVX512:
#ifdef __AVX512F__
		return 1;
#else
		return 0;
#endif
	default: return 0;
	}
}

typedef float (*dot_fn)(const float *, const float *, uint32_t);

static dot_fn get_dot_fn(dot_method_t m)
{
	switch (m) {
	case DOT_SCALAR: return dot_scalar;
#ifdef __SSE4_2__
	case DOT_SSE42:  return dot_sse42;
#endif
#ifdef __AVX2__
	case DOT_AVX2:   return dot_avx2;
#endif
#ifdef __AVX512F__
	case DOT_AVX512: return dot_avx512;
#endif
	default:         return NULL;
	}
}

#endif /* BENCH_SIMD_H */
