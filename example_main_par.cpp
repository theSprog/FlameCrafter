#include "./include/flamegraph.hpp"
#include "./include/parallel_flamegraph.hpp"
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_perf_file> <output_svg_file>\n";
        return 1;
    }

    try {
        flamegraph::FlameGraphConfig config;
        config.title = "Par CPU Flame Graph";
        config.interactive = true;

        // 测量并行版本的时间
        auto start = std::chrono::high_resolution_clock::now();

        flamegraph::ParallelFlameGraphGenerator parallel_generator(config);
        parallel_generator.generate_from(argv[1], argv[2]);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Parallel generation completed in " << duration.count() << " ms\n";

        // 可选：比较单线程版本
        if (false) { // 设置为 true 以进行比较
            start = std::chrono::high_resolution_clock::now();

            flamegraph::FlameGraphGenerator serial_generator(config);
            serial_generator.generate_from(argv[1], "serial_" + std::string(argv[2]));

            end = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            std::cout << "Serial generation completed in " << duration.count() << " ms\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
