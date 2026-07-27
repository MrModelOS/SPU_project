"""
Low-level ctypes bindings for libsppu.so.
"""

import ctypes
import ctypes.util
import os
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants (must match sppu.h / sppu_device.h)
# ---------------------------------------------------------------------------

SPPU_SEU_MIN_DEPTH = 5
SPPU_SEU_MAX_DEPTH = 8
SPPU_SEU_VARIANTS = 16
SPPU_SEU_TREE_ENTRIES = SPPU_SEU_VARIANTS * SPPU_SEU_MAX_DEPTH  # 128

# Tree config field encodings (must match sppu_device.h)
SPPU_TREE_CFG_AUTO_VALIDATE = 1 << 8

# ---------------------------------------------------------------------------
# Load libsppu.so
# ---------------------------------------------------------------------------

_LIB_SEARCH_PATHS = [
    Path(__file__).resolve().parent.parent / "libsppu.so",
    Path(__file__).resolve().parent.parent.parent / "sdk" / "libsppu.so",
    Path("/usr/local/lib/libsppu.so"),
    Path("/usr/lib/libsppu.so"),
]


def _find_libsppu():
    """Locate libsppu.so using search paths or ldconfig."""
    for p in _LIB_SEARCH_PATHS:
        if p.exists():
            return str(p)
    found = ctypes.util.find_library("sppu")
    if found:
        return found
    raise OSError(
        "Cannot find libsppu.so. Build the SDK first: cd sdk && make"
    )


_lib = ctypes.CDLL(_find_libsppu())

# ---------------------------------------------------------------------------
# C function prototypes
# ---------------------------------------------------------------------------

# sppu_t *sppu_open(const char *path)
_lib.sppu_open.argtypes = [ctypes.c_char_p]
_lib.sppu_open.restype = ctypes.c_void_p

# void sppu_close(sppu_t *h)
_lib.sppu_close.argtypes = [ctypes.c_void_p]
_lib.sppu_close.restype = None

# int sppu_reset(sppu_t *h)
_lib.sppu_reset.argtypes = [ctypes.c_void_p]
_lib.sppu_reset.restype = ctypes.c_int

# int sppu_configure(sppu_t *h, uint32_t vec_count, uint32_t dimension)
_lib.sppu_configure.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
_lib.sppu_configure.restype = ctypes.c_int

# int sppu_load_vector(sppu_t *h, uint32_t index, const float *data, uint32_t dim)
_lib.sppu_load_vector.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint32,
]
_lib.sppu_load_vector.restype = ctypes.c_int

# int sppu_set_target(sppu_t *h, const float *data, uint32_t dim)
_lib.sppu_set_target.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint32,
]
_lib.sppu_set_target.restype = ctypes.c_int

# int sppu_start(sppu_t *h)
_lib.sppu_start.argtypes = [ctypes.c_void_p]
_lib.sppu_start.restype = ctypes.c_int

# int sppu_get_status(sppu_t *h, uint32_t *status)
_lib.sppu_get_status.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
_lib.sppu_get_status.restype = ctypes.c_int

# int sppu_get_result(sppu_t *h, uint32_t *index, float *score, uint32_t *status)
_lib.sppu_get_result.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
]
_lib.sppu_get_result.restype = ctypes.c_int

# int sppu_wait_result(...)
_lib.sppu_wait_result.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.c_uint,
]
_lib.sppu_wait_result.restype = ctypes.c_int

# int sppu_seu_configure(sppu_t *h, uint32_t depth, uint32_t offset,
#                        uint32_t tree_addr, uint32_t prob_base)
_lib.sppu_seu_configure.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
]
_lib.sppu_seu_configure.restype = ctypes.c_int

# int sppu_seu_start(sppu_t *h)
_lib.sppu_seu_start.argtypes = [ctypes.c_void_p]
_lib.sppu_seu_start.restype = ctypes.c_int

# int sppu_seu_get_tree(sppu_t *h, float entries[128], uint32_t *status)
_lib.sppu_seu_get_tree.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
]
_lib.sppu_seu_get_tree.restype = ctypes.c_int

# int sppu_predict_tree(...)
_lib.sppu_predict_tree.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint,
]
_lib.sppu_predict_tree.restype = ctypes.c_int

# int sppu_seu_build_tree(...)
_lib.sppu_seu_build_tree.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_uint16,
]
_lib.sppu_seu_build_tree.restype = ctypes.c_int

# int sppu_seu_get_branches(...)
_lib.sppu_seu_get_branches.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint16),
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_uint32),
]
_lib.sppu_seu_get_branches.restype = ctypes.c_int

# int sppu_seu_accept(...)
_lib.sppu_seu_accept.argtypes = [ctypes.c_void_p]
_lib.sppu_seu_accept.restype = ctypes.c_int

# int sppu_seu_rollback(...)
_lib.sppu_seu_rollback.argtypes = [ctypes.c_void_p]
_lib.sppu_seu_rollback.restype = ctypes.c_int


# ---------------------------------------------------------------------------
# Python exceptions
# ---------------------------------------------------------------------------


class SPPUError(Exception):
    """SPPU operation failed."""

    pass


# ---------------------------------------------------------------------------
# SPPU class
# ---------------------------------------------------------------------------


class SPPU:
    """
    High-level Python interface to the SPPU (Search Processing Unit).

    Usage::

        with SPPU() as sppu:
            sppu.configure(vec_count=1000, dimension=128)
            for i, vec in enumerate(vectors):
                sppu.load_vector(i, vec)
            sppu.set_target(query)
            idx, score = sppu.search(timeout_ms=5000)
            print(f"Best match: vector #{idx}, score={score:.4f}")
    """

    def __init__(self, device_path=None):
        """
        Open SPPU device.

        :param device_path: Path to /dev/sppu, or None for default
        """
        path = device_path.encode("utf-8") if device_path else None
        self._h = _lib.sppu_open(path)
        if not self._h:
            raise SPPUError(f"Failed to open SPPU device: {device_path or '/dev/sppu'}")

    def close(self):
        """Close device handle. Safe to call multiple times."""
        if self._h:
            _lib.sppu_close(self._h)
            self._h = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()

    # ---- SPPU core ----

    def reset(self):
        """Reset SPPU pipeline."""
        if _lib.sppu_reset(self._h) < 0:
            raise SPPUError("sppu_reset failed")

    def configure(self, vec_count, dimension):
        """
        Configure SPPU search parameters.

        :param vec_count: Number of vectors in memory (max 1000)
        :param dimension: Vector dimension (max 768)
        """
        if _lib.sppu_configure(self._h, vec_count, dimension) < 0:
            raise SPPUError("sppu_configure failed")

    def load_vector(self, index, data):
        """
        Load a single vector into SPPU memory.

        :param index: Vector index (0..vec_count-1)
        :param data: list/tuple of floats, or numpy array
        """
        dim = len(data)
        arr = (ctypes.c_float * dim)(*data)
        if _lib.sppu_load_vector(self._h, index, arr, dim) < 0:
            raise SPPUError(f"sppu_load_vector({index}) failed")

    def set_target(self, data):
        """
        Set the target (query) vector.

        :param data: list/tuple of floats, or numpy array
        """
        dim = len(data)
        arr = (ctypes.c_float * dim)(*data)
        if _lib.sppu_set_target(self._h, arr, dim) < 0:
            raise SPPUError("sppu_set_target failed")

    def start(self):
        """Start SPPU search."""
        if _lib.sppu_start(self._h) < 0:
            raise SPPUError("sppu_start failed")

    def get_status(self):
        """Read current SPPU status register."""
        status = ctypes.c_uint32()
        if _lib.sppu_get_status(self._h, status) < 0:
            raise SPPUError("sppu_get_status failed")
        return status.value

    def get_result(self):
        """Read search result without waiting."""
        idx = ctypes.c_uint32()
        score = ctypes.c_float()
        status = ctypes.c_uint32()
        if _lib.sppu_get_result(self._h, idx, score, status) < 0:
            raise SPPUError("sppu_get_result failed")
        return idx.value, score.value, status.value

    def wait_result(self, timeout_ms=5000):
        """
        Start search and wait for completion.

        :param timeout_ms: Max wait time (0 = indefinite)
        :return: (index, score)
        :raises SPPUError: on timeout or failure
        """
        self.start()
        idx = ctypes.c_uint32()
        score = ctypes.c_float()
        status = ctypes.c_uint32()
        if _lib.sppu_wait_result(self._h, idx, score, status, timeout_ms) < 0:
            raise SPPUError("sppu_wait_result failed (timeout?)")
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
        if depth < SPPU_SEU_MIN_DEPTH or depth > SPPU_SEU_MAX_DEPTH:
            raise ValueError(
                f"depth must be {SPPU_SEU_MIN_DEPTH}..{SPPU_SEU_MAX_DEPTH}, got {depth}"
            )
        if _lib.sppu_seu_configure(self._h, depth, offset, tree_addr, prob_base) < 0:
            raise SPPUError("sppu_seu_configure failed")

    def seu_start(self):
        """Start SEU tree generation."""
        if _lib.sppu_seu_start(self._h) < 0:
            raise SPPUError("sppu_seu_start failed")

    def seu_get_tree(self):
        """
        Read SEU prediction tree results.

        :return: (entries: list[float], status: int)
        """
        entries = (ctypes.c_float * SPPU_SEU_TREE_ENTRIES)()
        status = ctypes.c_uint32()
        if _lib.sppu_seu_get_tree(self._h, entries, status) < 0:
            raise SPPUError("sppu_seu_get_tree failed")
        return list(entries), status.value

    def predict_tree(self, depth, offset, timeout_ms=5000):
        """
        Configure, start, and wait for SEU tree generation.

        :param depth: Tree depth (5..8)
        :param offset: Branch offsets
        :param timeout_ms: Max wait time
        :return: list of 128 probability scores
        """
        entries = (ctypes.c_float * SPPU_SEU_TREE_ENTRIES)()
        if _lib.sppu_predict_tree(self._h, depth, offset, entries, timeout_ms) < 0:
            raise SPPUError("sppu_predict_tree failed")
        return list(entries)

    # ---- SEU v0.4 — Speculative Tree Walker ----

    def generate_speculative_tree(self, context_tokens, max_branches=16,
                                   context_len=None, auto_validate=True,
                                   node_base=0, branch_mask=0xFFFF,
                                   embed_addr=0, timeout_ms=5000):
        """
        High-level API: generate a speculative tree from context tokens.

        :param context_tokens: list of token values (up to 8 tokens)
        :param max_branches: number of branches to explore (1..16)
        :param context_len: number of context tokens (auto-detected if None)
        :param auto_validate: if True, auto-validate via embedding search
        :param node_base: base address for tree node weights in vmem
        :param branch_mask: bitmask of active branches (16-bit)
        :param embed_addr: address of embedding search target in vmem
        :param timeout_ms: max wait time
        :return: dict with keys: branch_valid, best_idx, best_score, tree_entries
        """
        if context_len is None:
            context_len = len(context_tokens)
        if context_len > 8:
            raise ValueError("context_tokens must have at most 8 entries")
        if max_branches < 1 or max_branches > SPPU_SEU_VARIANTS:
            raise ValueError(f"max_branches must be 1..{SPPU_SEU_VARIANTS}")

        # Pack context tokens into context address (use offset 0x10000 in vmem)
        context_addr = 0x10000
        for i, token in enumerate(context_tokens):
            self.load_vector(i, [float(token)])

        # Build tree
        if _lib.sppu_seu_build_tree(
            self._h,
            context_addr,
            embed_addr,
            max_branches,
            context_len,
            1 if auto_validate else 0,
            node_base,
            branch_mask,
        ) < 0:
            raise SPPUError("sppu_seu_build_tree failed")

        # Wait for completion
        import time
        deadline = time.monotonic() + timeout_ms / 1000.0
        while time.monotonic() < deadline:
            branch_valid = ctypes.c_uint16()
            best_idx = ctypes.c_uint32()
            best_score = ctypes.c_float()
            tree_entries = ctypes.c_uint32()

            if _lib.sppu_seu_get_branches(
                self._h, branch_valid, best_idx, best_score, tree_entries
            ) < 0:
                raise SPPUError("sppu_seu_get_branches failed")

            if tree_entries.value > 0:
                return {
                    "branch_valid": branch_valid.value,
                    "best_idx": best_idx.value,
                    "best_score": best_score.value,
                    "tree_entries": tree_entries.value,
                }

            time.sleep(0.001)

        raise SPPUError("generate_speculative_tree timed out")

    def accept_tree(self):
        """Accept the current speculative tree (no rollback)."""
        if _lib.sppu_seu_accept(self._h) < 0:
            raise SPPUError("sppu_seu_accept failed")

    def rollback_tree(self):
        """Rollback the current speculative tree (invalidate branches)."""
        if _lib.sppu_seu_rollback(self._h) < 0:
            raise SPPUError("sppu_seu_rollback failed")
