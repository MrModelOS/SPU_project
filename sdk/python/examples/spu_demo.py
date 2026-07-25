#!/usr/bin/env python3
"""
SPU Python SDK Demo
Demonstrates: vector search, SEU prediction tree, probability profiling.

Usage:
    1. Load kernel module:  sudo insmod kernel_module/spu_driver.ko emulation=1
    2. Run:                 python3 sdk/python/examples/spu_demo.py
"""

import random
import sys
import os

# Allow running from repo root
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spu import SPU, SPUError, SPU_SEU_TREE_ENTRIES


def frand():
    return random.uniform(-1.0, 1.0)


def demo_vector_search(spu):
    """Run a basic similarity search: 10 vectors, dim=8."""
    print("=== SPU Vector Search ===")

    spu.reset()
    spu.configure(vec_count=10, dimension=8)

    # Generate 10 random vectors
    vectors = [[frand() for _ in range(8)] for _ in range(10)]

    # Load all vectors
    for i, vec in enumerate(vectors):
        spu.load_vector(i, vec)

    # Set target = copy of vector #7
    target = vectors[7][:]
    spu.load_vector(7, target)
    spu.set_target(target)

    # Search
    idx, score = spu.search(timeout_ms=5000)
    print(f"  Best match: vector #{idx}, score={score:.4f}")

    if idx == 7:
        print("  Validation PASSED — exact match at #7")
    else:
        print(f"  Validation FAILED — expected #7, got #{idx}")
    print()


def demo_seu_tree(spu):
    """Generate a prediction tree with SEU."""
    print("=== SEU Prediction Tree ===")

    depth = 6
    offset = 0xF0F0

    entries = spu.predict_tree(depth=depth, offset=offset, timeout_ms=5000)
    print(f"  Depth: {depth}, Offset: 0x{offset:04X}")
    print(f"  Generated {len(entries)} probability entries")

    # Show first 16 entries
    print("  First 16 entries:")
    for i in range(min(16, len(entries))):
        print(f"    [{i:3d}] {entries[i]:.6f}")
    print()


def demo_probability_profiling(spu):
    """Read back probability entries after tree generation."""
    print("=== SEU Probability Profiling ===")

    depth = 8
    offset = 0x1234

    entries = spu.predict_tree(depth=depth, offset=offset, timeout_ms=5000)
    print(f"  Generated {len(entries)} entries (depth={depth})")

    # Compute statistics
    if entries:
        min_p = min(entries)
        max_p = max(entries)
        avg_p = sum(entries) / len(entries)
        print(f"  Min probability:  {min_p:.6f}")
        print(f"  Max probability:  {max_p:.6f}")
        print(f"  Avg probability:  {avg_p:.6f}")
        print(f"  Dynamic range:    {max_p - min_p:.6f}")
    print()


def main():
    print("SPU Python SDK Demo v0.3\n")

    try:
        with SPU() as spu:
            demo_vector_search(spu)
            demo_seu_tree(spu)
            demo_probability_profiling(spu)
            print("All demos completed successfully.")
    except SPUError as e:
        print(f"SPU Error: {e}", file=sys.stderr)
        print(
            "Make sure the kernel module is loaded:\n"
            "  sudo insmod kernel_module/spu_driver.ko emulation=1",
            file=sys.stderr,
        )
        sys.exit(1)
    except OSError as e:
        print(f"OS Error: {e}", file=sys.stderr)
        print(
            "Make sure libspu.so is built:\n"
            "  cd sdk && make",
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
