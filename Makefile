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

TARGET = flamegraph_main
SOURCE = example_main.cpp
HEADER_DIR = include
HEADER = $(HEADER_DIR)/flamegraph.hpp
BUILD_DIR = build

# Default target
all: $(TARGET)

# Build the example
$(TARGET): $(SOURCE) $(HEADER)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

# Run the example
run: $(TARGET)
	./$(TARGET)

# Clean generated files
clean:
	rm -f $(TARGET)
	rm -f *.svg *.html
	rm -f *.txt
	rm -f *.folded
	rm -f *_test.txt *_flamegraph.html *_flamegraph.svg
	rm -f *.collapse
	rm -f flamegraph.svg
	find build/ -type f ! -name 'holder' -exec rm -f {} +

# Performance tests with different data sizes
perf-small: $(TARGET)
	@echo "🔥 Small performance test (1K samples)..."
	python3 script/generate_test_data.py --small
	@time ./$(TARGET) small_test.txt $(BUILD_DIR)/small_flamegraph.html 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./$(TARGET) small_test.txt $(BUILD_DIR)/small_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

perf-medium: $(TARGET)
	@echo "🔥 Medium performance test (10K samples)..."
	python3 script/generate_test_data.py --medium
	@time ./$(TARGET) medium_test.txt $(BUILD_DIR)/medium_flamegraph.html 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./$(TARGET) medium_test.txt $(BUILD_DIR)/medium_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

perf-large: $(TARGET)
	@echo "🔥 Large performance test (100K samples)..."
	python3 script/generate_test_data.py --large
	@time ./$(TARGET) large_test.txt $(BUILD_DIR)/large_flamegraph.html 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./$(TARGET) large_test.txt $(BUILD_DIR)/large_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

perf-huge: $(TARGET)
	@echo "🔥 Huge performance test (1M samples)..."
	python3 script/generate_test_data.py --huge
	@time ./$(TARGET) huge_test.txt $(BUILD_DIR)/huge_flamegraph.html 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"
	@time ./$(TARGET) huge_test.txt $(BUILD_DIR)/huge_flamegraph.svg 2>/dev/null || echo "❌ Note: Modify main.cpp to accept command line arguments"

# Run all performance tests
perf-all: perf-small perf-medium perf-large perf-huge

# Comprehensive benchmark
benchmark: $(TARGET)
	@echo "🏆 Running comprehensive benchmark..."
	python3 bench/benchmark.py

# Help
help:
	@echo "Available targets:"
	@echo "  all           - Build the example program"
	@echo "  run           - Build and run the example"
	@echo "  clean         - Remove all generated files"
	@echo "  perf-small    - Performance test with 1K samples"
	@echo "  perf-medium   - Performance test with 10K samples"
	@echo "  perf-large    - Performance test with 100K samples"
	@echo "  perf-huge     - Performance test with 1M samples"
	@echo "  perf-all      - Run all performance tests"
	@echo "  benchmark     - Run comprehensive benchmark"
	@echo "  help          - Show this help message"

.PHONY: all run clean clean-svg install generate-data perf-small perf-medium perf-large perf-all benchmark help