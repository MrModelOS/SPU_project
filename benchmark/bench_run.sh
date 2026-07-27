#!/bin/bash
# bench_run.sh — Run SPPU benchmark with different configurations
# Usage: ./bench_run.sh [--full]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH="$SCRIPT_DIR/bench_search"

if [ ! -f "$BENCH" ]; then
    echo "Building benchmark..."
    make -C "$SCRIPT_DIR"
fi

echo "========================================="
echo " SPPU Benchmark Suite"
echo "========================================="

if [ "$1" = "--full" ]; then
    echo "Running full benchmark suite..."
    "$BENCH" --full
else
    echo ""
    echo "--- Quick test: 10K vectors, dim=128 ---"
    "$BENCH" --vectors 10000 --dim 128 --repeats 10

    echo ""
    echo "--- Scale test: 100K vectors, dim=128 ---"
    "$BENCH" --vectors 100000 --dim 128 --repeats 3

    echo ""
    echo "--- Dimension test: 10K vectors, dim=768 ---"
    "$BENCH" --vectors 10000 --dim 768 --repeats 5
fi

echo ""
echo "========================================="
echo " Benchmark complete"
echo "========================================="
