# C++17 Flame Graph Generator Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
TARGET = flamegraph_main
SOURCE = example_main.cpp
HEADER = include/flamegraph.hpp

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

# Install (copy header to system include path - optional)
install:
	sudo cp $(HEADER) /usr/local/include/

# Generate test data
generate-data:
	python3 generate_test_data.py --medium

# Performance tests with different data sizes
perf-small: $(TARGET)
	@echo "🔥 Small performance test (1K samples)..."
	python3 generate_test_data.py --small
	@time ./$(TARGET) small_test.txt small_flamegraph.html 2>/dev/null || echo "Note: Modify main.cpp to accept command line arguments"

perf-medium: $(TARGET)
	@echo "🔥 Medium performance test (10K samples)..."
	python3 generate_test_data.py --medium
	@time ./$(TARGET) medium_test.txt medium_flamegraph.html 2>/dev/null || echo "Note: Modify main.cpp to accept command line arguments"

perf-large: $(TARGET)
	@echo "🔥 Large performance test (100K samples)..."
	python3 generate_test_data.py --large
	@time ./$(TARGET) large_test.txt large_flamegraph.html 2>/dev/null || echo "Note: Modify main.cpp to accept command line arguments"

perf-huge: $(TARGET)
	@echo "🔥 Huge performance test (1M samples)..."
	python3 generate_test_data.py --huge
	@time ./$(TARGET) huge_test.txt huge_flamegraph.html 2>/dev/null || echo "Note: Modify main.cpp to accept command line arguments"

# Run all performance tests
perf-all: perf-small perf-medium perf-large perf-huge

# Comprehensive benchmark
benchmark: $(TARGET)
	@echo "🏆 Running comprehensive benchmark..."
	python3 benchmark.py

# Help
help:
	@echo "Available targets:"
	@echo "  all           - Build the example program"
	@echo "  run           - Build and run the example"
	@echo "  clean         - Remove all generated files"
	@echo "  install       - Copy header to system include path"
	@echo "  generate-data - Generate medium test dataset"
	@echo "  perf-small    - Performance test with 1K samples"
	@echo "  perf-medium   - Performance test with 10K samples"
	@echo "  perf-large    - Performance test with 100K samples"
	@echo "  perf-huge     - Performance test with 1M samples"
	@echo "  perf-all      - Run all performance tests"
	@echo "  benchmark     - Run comprehensive benchmark"
	@echo "  help          - Show this help message"

.PHONY: all run clean clean-svg install generate-data perf-small perf-medium perf-large perf-all benchmark help