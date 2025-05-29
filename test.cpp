#include "./include/flamegraph.hpp"
#include <iostream>

int main() {
    using namespace flamegraph;

    try {
        // 1️⃣ 解析 perf.perf 文件
        PerfScriptParser parser;
        auto samples = parser.parse("perf.perf");
        std::cout << "✅ PerfScriptParser 解析完成: " << samples.size() << " 个样本\n";

        // 2️⃣ 折叠
        StackCollapser collapser;
        auto folded = collapser.collapse(samples);
        std::cout << "✅ 折叠完成: " << folded.size() << " 种调用链\n";

        // 3️⃣ 构建树
        FlameGraphBuilder builder;
        auto root = builder.build_tree(folded);
        std::cout << "✅ 树构建完成，总样本: " << root->total_count << "\n";

        // 4️⃣ 渲染 SVG
        FlameGraphConfig config;
        config.title = "🔥 Perf FlameGraph";
        config.width = 1200;
        config.height = 800;
        // config.colors = "hot"; // 或者 "cool"
        config.colors = "hot";

        config.font_size = 12;
        config.interactive = true;

        SvgFlameGraphRenderer renderer(config);
        renderer.render(*root, "flamegraph.svg");
        std::cout << "✅ SVG 渲染完成: flamegraph.svg\n";

    } catch (const FlameGraphException& e) {
        std::cerr << "❌ FlameGraphException: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "❌ std::exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
