"""
sppu — Python bindings for the SPPU (Search Processing Unit) SDK.

Thin ctypes wrapper around libsppu.so providing:
- SPPU similarity search (dot-product engine)
- SEU predictive tree generation
- Runtime probability profiling
"""

from sppu.bindings import (
    SPPU,
    SPPUError,
    SPPU_SEU_MIN_DEPTH,
    SPPU_SEU_MAX_DEPTH,
    SPPU_SEU_VARIANTS,
    SPPU_SEU_TREE_ENTRIES,
)

__version__ = "0.3.0"
__all__ = [
    "SPPU",
    "SPPUError",
    "SPPU_SEU_MIN_DEPTH",
    "SPPU_SEU_MAX_DEPTH",
    "SPPU_SEU_VARIANTS",
    "SPPU_SEU_TREE_ENTRIES",
]
