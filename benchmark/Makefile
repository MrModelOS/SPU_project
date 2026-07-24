# SPU Benchmark — dot-product search performance

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11

# Detect and enable SIMD
SIMD_FLAGS := -msse4.2 -mavx2 -mavx512f -march=native

TARGET  = bench_search
SCRIPT  = bench_run.sh

.PHONY: all clean run run-full

all: $(TARGET)

$(TARGET): bench_search.c bench_simd.h
	$(CC) $(CFLAGS) $(SIMD_FLAGS) -O2 -o $@ $< -lm

run: $(TARGET)
	./$(TARGET)

run-full: $(TARGET)
	./$(TARGET) --full

clean:
	rm -f $(TARGET)
