"""
Low-level ctypes bindings for libspu.so.
"""

import ctypes
import ctypes.util
import os
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants (must match spu.h / spu_device.h)
# ---------------------------------------------------------------------------

SPU_SEU_MIN_DEPTH = 5
SPU_SEU_MAX_DEPTH = 8
SPU_SEU_VARIANTS = 16
SPU_SEU_TREE_ENTRIES = SPU_SEU_VARIANTS * SPU_SEU_MAX_DEPTH  # 128

# ---------------------------------------------------------------------------
# Load libspu.so
# ---------------------------------------------------------------------------

_LIB_SEARCH_PATHS = [
    Path(__file__).resolve().parent.parent / "libspu.so",
    Path(__file__).resolve().parent.parent.parent / "sdk" / "libspu.so",
    Path("/usr/local/lib/libspu.so"),
    Path("/usr/lib/libspu.so"),
]


def _find_libspu():
    """Locate libspu.so using search paths or ldconfig."""
    for p in _LIB_SEARCH_PATHS:
        if p.exists():
            return str(p)
    found = ctypes.util.find_library("spu")
    if found:
        return found
    raise OSError(
        "Cannot find libspu.so. Build the SDK first: cd sdk && make"
    )


_lib = ctypes.CDLL(_find_libspu())

# ---------------------------------------------------------------------------
# C function prototypes
# ---------------------------------------------------------------------------

# spu_t *spu_open(const char *path)
_lib.spu_open.argtypes = [ctypes.c_char_p]
_lib.spu_open.restype = ctypes.c_void_p

# void spu_close(spu_t *h)
_lib.spu_close.argtypes = [ctypes.c_void_p]
_lib.spu_close.restype = None

# int spu_reset(spu_t *h)
_lib.spu_reset.argtypes = [ctypes.c_void_p]
_lib.spu_reset.restype = ctypes.c_int

# int spu_configure(spu_t *h, uint32_t vec_count, uint32_t dimension)
_lib.spu_configure.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
_lib.spu_configure.restype = ctypes.c_int

# int spu_load_vector(spu_t *h, uint32_t index, const float *data, uint32_t dim)
_lib.spu_load_vector.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint32,
]
_lib.spu_load_vector.restype = ctypes.c_int

# int spu_set_target(spu_t *h, const float *data, uint32_t dim)
_lib.spu_set_target.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint32,
]
_lib.spu_set_target.restype = ctypes.c_int

# int spu_start(spu_t *h)
_lib.spu_start.argtypes = [ctypes.c_void_p]
_lib.spu_start.restype = ctypes.c_int

# int spu_get_status(spu_t *h, uint32_t *status)
_lib.spu_get_status.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
_lib.spu_get_status.restype = ctypes.c_int

# int spu_get_result(spu_t *h, uint32_t *index, float *score, uint32_t *status)
_lib.spu_get_result.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
]
_lib.spu_get_result.restype = ctypes.c_int

# int spu_wait_result(...)
_lib.spu_wait_result.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.c_uint,
]
_lib.spu_wait_result.restype = ctypes.c_int

# int spu_seu_configure(spu_t *h, uint32_t depth, uint32_t offset,
#                        uint32_t tree_addr, uint32_t prob_base)
_lib.spu_seu_configure.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
]
_lib.spu_seu_configure.restype = ctypes.c_int

# int spu_seu_start(spu_t *h)
_lib.spu_seu_start.argtypes = [ctypes.c_void_p]
_lib.spu_seu_start.restype = ctypes.c_int

# int spu_seu_get_tree(spu_t *h, float entries[128], uint32_t *status)
_lib.spu_seu_get_tree.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
]
_lib.spu_seu_get_tree.restype = ctypes.c_int

# int spu_predict_tree(...)
_lib.spu_predict_tree.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint,
]
_lib.spu_predict_tree.restype = ctypes.c_int


# ---------------------------------------------------------------------------
# Python exceptions
# ---------------------------------------------------------------------------


class SPUError(Exception):
    """SPU operation failed."""

    pass


# ---------------------------------------------------------------------------
# SPU class
# ---------------------------------------------------------------------------


class SPU:
    """
    High-level Python interface to the SPU (Search Processing Unit).

    Usage::

        with SPU() as spu:
            spu.configure(vec_count=1000, dimension=128)
            for i, vec in enumerate(vectors):
                spu.load_vector(i, vec)
            spu.set_target(query)
            idx, score = spu.search(timeout_ms=5000)
            print(f"Best match: vector #{idx}, score={score:.4f}")
    """

    def __init__(self, device_path=None):
        """
        Open SPU device.

        :param device_path: Path to /dev/spu, or None for default
        """
        path = device_path.encode("utf-8") if device_path else None
        self._h = _lib.spu_open(path)
        if not self._h:
            raise SPUError(f"Failed to open SPU device: {device_path or '/dev/spu'}")

    def close(self):
        """Close device handle. Safe to call multiple times."""
        if self._h:
            _lib.spu_close(self._h)
            self._h = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()

    # ---- SPU core ----

    def reset(self):
        """Reset SPU pipeline."""
        if _lib.spu_reset(self._h) < 0:
            raise SPUError("spu_reset failed")

    def configure(self, vec_count, dimension):
        """
        Configure SPU search parameters.

        :param vec_count: Number of vectors in memory (max 1000)
        :param dimension: Vector dimension (max 768)
        """
        if _lib.spu_configure(self._h, vec_count, dimension) < 0:
            raise SPUError("spu_configure failed")

    def load_vector(self, index, data):
        """
        Load a single vector into SPU memory.

        :param index: Vector index (0..vec_count-1)
        :param data: list/tuple of floats, or numpy array
        """
        dim = len(data)
        arr = (ctypes.c_float * dim)(*data)
        if _lib.spu_load_vector(self._h, index, arr, dim) < 0:
            raise SPUError(f"spu_load_vector({index}) failed")

    def set_target(self, data):
        """
        Set the target (query) vector.

        :param data: list/tuple of floats, or numpy array
        """
        dim = len(data)
        arr = (ctypes.c_float * dim)(*data)
        if _lib.spu_set_target(self._h, arr, dim) < 0:
            raise SPUError("spu_set_target failed")

    def start(self):
        """Start SPU search."""
        if _lib.spu_start(self._h) < 0:
            raise SPUError("spu_start failed")

    def get_status(self):
        """Read current SPU status register."""
        status = ctypes.c_uint32()
        if _lib.spu_get_status(self._h, status) < 0:
            raise SPUError("spu_get_status failed")
        return status.value

    def get_result(self):
        """Read search result without waiting."""
        idx = ctypes.c_uint32()
        score = ctypes.c_float()
        status = ctypes.c_uint32()
        if _lib.spu_get_result(self._h, idx, score, status) < 0:
            raise SPUError("spu_get_result failed")
        return idx.value, score.value, status.value

    def wait_result(self, timeout_ms=5000):
        """
        Start search and wait for completion.

        :param timeout_ms: Max wait time (0 = indefinite)
        :return: (index, score)
        :raises SPUError: on timeout or failure
        """
        self.start()
        idx = ctypes.c_uint32()
        score = ctypes.c_float()
        status = ctypes.c_uint32()
        if _lib.spu_wait_result(self._h, idx, score, status, timeout_ms) < 0:
            raise SPUError("spu_wait_result failed (timeout?)")
        return idx.value, score.value

    def search(self, timeout_ms=5000):
        """Alias for wait_result()."""
        return self.wait_result(timeout_ms=timeout_ms)

    # ---- SEU (Speculative Execution Unit) ----

    def seu_configure(self, depth, offset, tree_addr=0, prob_base=0):
        """
        Configure SEU prediction tree.

        :param depth: Tree depth (5..8)
        :param offset: Branch offsets (packed 32-bit)
        :param tree_addr: Base address in vector memory
        :param prob_base: Base address for probability config
        """
        if depth < SPU_SEU_MIN_DEPTH or depth > SPU_SEU_MAX_DEPTH:
            raise ValueError(
                f"depth must be {SPU_SEU_MIN_DEPTH}..{SPU_SEU_MAX_DEPTH}, got {depth}"
            )
        if _lib.spu_seu_configure(self._h, depth, offset, tree_addr, prob_base) < 0:
            raise SPUError("spu_seu_configure failed")

    def seu_start(self):
        """Start SEU tree generation."""
        if _lib.spu_seu_start(self._h) < 0:
            raise SPUError("spu_seu_start failed")

    def seu_get_tree(self):
        """
        Read SEU prediction tree results.

        :return: (entries: list[float], status: int)
        """
        entries = (ctypes.c_float * SPU_SEU_TREE_ENTRIES)()
        status = ctypes.c_uint32()
        if _lib.spu_seu_get_tree(self._h, entries, status) < 0:
            raise SPUError("spu_seu_get_tree failed")
        return list(entries), status.value

    def predict_tree(self, depth, offset, timeout_ms=5000):
        """
        Configure, start, and wait for SEU tree generation.

        :param depth: Tree depth (5..8)
        :param offset: Branch offsets
        :param timeout_ms: Max wait time
        :return: list of 128 probability scores
        """
        entries = (ctypes.c_float * SPU_SEU_TREE_ENTRIES)()
        if _lib.spu_predict_tree(self._h, depth, offset, entries, timeout_ms) < 0:
            raise SPUError("spu_predict_tree failed")
        return list(entries)
