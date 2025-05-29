#include "./include/flamegraph.hpp"
#include <iostream>
#include <chrono>
#include <stdexcept>

using namespace flamegraph;

int main(int argc, char* argv[]) {
    try {
        // 检查命令行参数
        if (argc == 3) {
            // 性能测试模式：./flamegraph_example input.txt output.svg
            std::string input_file = argv[1];
            std::string output_file = argv[2];
            
            std::cout << "📊 Performance Test Mode\n";
            std::cout << "Input: " << input_file << "\n";
            std::cout << "Output: " << output_file << "\n\n";
            
            FlameGraphConfig config;
            config.title = "Performance Test Flame Graph";
            config.interactive = true;
            config.write_folded_file = true;
            
            FlameGraphGenerator generator(config);
            
            auto start = std::chrono::high_resolution_clock::now();
            generator.generate_from_raw(input_file, output_file);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "⏱️  Processing time: " << duration.count() << " ms\n";
            
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