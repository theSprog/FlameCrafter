#include "./include/flamegraph.hpp"
#include <iostream>
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

            generator.generate(argv[1], argv[2]);

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
