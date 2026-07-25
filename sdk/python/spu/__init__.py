"""
spu — Python bindings for the SPU (Search Processing Unit) SDK.

Thin ctypes wrapper around libspu.so providing:
- SPU similarity search (dot-product engine)
- SEU predictive tree generation
- Runtime probability profiling
"""

from spu.bindings import (
    SPU,
    SPUError,
    SPU_SEU_MIN_DEPTH,
    SPU_SEU_MAX_DEPTH,
    SPU_SEU_VARIANTS,
    SPU_SEU_TREE_ENTRIES,
)

__version__ = "0.3.0"
__all__ = [
    "SPU",
    "SPUError",
    "SPU_SEU_MIN_DEPTH",
    "SPU_SEU_MAX_DEPTH",
    "SPU_SEU_VARIANTS",
    "SPU_SEU_TREE_ENTRIES",
]
