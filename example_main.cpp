#include "./include/flamegraph.hpp"
#include <iostream>
#include <chrono>
#include <stdexcept>

using namespace flamegraph;

int main(int argc, char* argv[]) {
    try {
        if (argc == 3) { // 检查命令行参数
            FlameGraphConfig config;
            config.title = "Performance Test Flame Graph";
            config.interactive = true;
            config.write_folded_file = false;

            FlameGraphGenerator generator(config);

            auto start = std::chrono::high_resolution_clock::now();
            generator.generate_from(argv[1], argv[2]);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            return 0;
        } else {
            throw std::invalid_argument("not 3 argument");
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
