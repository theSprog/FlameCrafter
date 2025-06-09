# C++17 Flame Graph Generator Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
# CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -g -fsanitize=address,undefined,leak
CXXFLAGS += -Werror=uninitialized \
    -Werror=return-type \
    -Wconversion \
    -Wsign-compare \
    -Werror=unused-result \
    -Werror=suggest-override \
    -Wzero-as-null-pointer-constant \
    -Wmissing-declarations \
    -Wold-style-cast \
    -Werror=vla \
    -Wnon-virtual-dtor \
    -Wreturn-local-addr

TBB_LIBS  = -ltbb
LINK_FLAGS = 
# LINK_FLAGS = -labsl_base \
# 	-labsl_raw_logging_internal \
# 	-labsl_throw_delegate -labsl_raw_hash_set \
# 	-labsl_hashtablez_sampler \
# 	-labsl_hash \
# 	-labsl_synchronization

TARGET = flamegraph_main flamegraph_main_par
SOURCE = example_main.cpp example_main_par.cpp
HEADER_DIR = include
HEADER = $(HEADER_DIR)/flamegraph.hpp
BUILD_DIR = build

# Default target
all: $(TARGET)

# Build the example
flamegraph_main: example_main.cpp $(HEADER)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LINK_FLAGS)

flamegraph_main_par: example_main_par.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(TBB_LIBS) $(LINK_FLAGS)

# Run the example
run: flamegraph_main
	./flamegraph_main

run-par: flamegraph_main_par
	./flamegraph_main_par

# Clean generated files
clean:
	rm -f $(TARGET)
	rm -f *.svg *.html *.json
	rm -f *.txt *.perf
	rm -f *.folded
	rm -f *_test.txt *_flamegraph.html *_flamegraph.svg
	rm -f *.collapse
	rm -f flamegraph.svg
	find build/ -type f ! -name 'holder' -exec rm -f {} +
	rm -f perf.data*

# Performance tests with different data sizes
perf-small: $(TARGETS)
	@echo "🔥 Small performance test (1K samples)..."
	python3 script/generate_test_data.py --small
	@time ./flamegraph_main small_test.txt $(BUILD_DIR)/small_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./flamegraph_main_par small_test.txt $(BUILD_DIR)/small_flamegraph_par.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

perf-medium: $(TARGETS)
	@echo "🔥 Medium performance test (10K samples)..."
	python3 script/generate_test_data.py --medium
	@time ./flamegraph_main medium_test.txt $(BUILD_DIR)/medium_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./flamegraph_main_par medium_test.txt $(BUILD_DIR)/medium_flamegraph_par.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

perf-large: $(TARGETS)
	@echo "🔥 Large performance test (100K samples)..."
	python3 script/generate_test_data.py --large
	@time ./flamegraph_main large_test.txt $(BUILD_DIR)/large_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./flamegraph_main_par large_test.txt $(BUILD_DIR)/large_flamegraph_par.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

perf-huge: $(TARGETS)
	@echo "🔥 Huge performance test (1M samples)..."
	python3 script/generate_test_data.py --huge
	@time ./flamegraph_main huge_test.txt $(BUILD_DIR)/huge_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./flamegraph_main_par huge_test.txt $(BUILD_DIR)/huge_flamegraph_par.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

# Run all performance tests
perf-all: perf-small perf-medium perf-large perf-huge

# Comprehensive benchmark
benchmark: $(TARGET)
	@echo "🏆 Running comprehensive benchmark..."
	python3 bench/benchmark.py

# Help
help:
	@echo "Available targets:"
	@echo "  all            - Build both flamegraph_main and flamegraph_main_par"
	@echo "  run            - Run flamegraph_main"
	@echo "  run-par        - Run flamegraph_main_par"
	@echo "  clean          - Remove all generated files"
	@echo "  perf-small     - Small performance test"
	@echo "  perf-medium    - Medium performance test"
	@echo "  perf-large     - Large performance test"
	@echo "  perf-huge      - Huge performance test"
	@echo "  perf-all       - Run all performance tests"
	@echo "  benchmark      - Run comprehensive benchmark"
	@echo "  help           - Show this help message"

.PHONY: all run clean clean-svg install generate-data perf-small perf-medium perf-large perf-all benchmark help