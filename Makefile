# SPU Project — top-level Makefile
# Builds all components: emulator, SDK, tools, benchmark, HTTP daemon

.PHONY: all clean tools sdk benchmark searchd emulator test run-bench

all: emulator sdk tools benchmark searchd

emulator:
	$(MAKE) -C emulator

sdk:
	$(MAKE) -C sdk

tools: sdk
	$(MAKE) -C tools

benchmark:
	$(MAKE) -C benchmark

searchd: sdk
	$(MAKE) -C examples/semantic_search

test: sdk
	$(MAKE) -C sdk test

run-bench: benchmark
	$(MAKE) -C benchmark run

run-searchd: searchd
	$(MAKE) -C examples/semantic_search run

clean:
	$(MAKE) -C emulator clean
	$(MAKE) -C sdk clean
	$(MAKE) -C tools clean
	$(MAKE) -C benchmark clean
	$(MAKE) -C examples/semantic_search clean
