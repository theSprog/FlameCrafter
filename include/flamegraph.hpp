#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <stdexcept>
#include <regex>
#include <cassert>

namespace flamegraph {

// 🔥 ===== 异常处理 =====

class FlameGraphException : public std::runtime_error {
  public:
    explicit FlameGraphException(const std::string& message) : std::runtime_error("FlameGraph Error: " + message) {}
};

class ParseException : public FlameGraphException {
  public:
    explicit ParseException(const std::string& message) : FlameGraphException("Parse Error: " + message) {}
};

class RenderException : public FlameGraphException {
  public:
    explicit RenderException(const std::string& message) : FlameGraphException("Render Error: " + message) {}
};

// 🔥 ===== 工具函数 =====

namespace utils {
inline std::string trim(const std::string& str) {
    if (str.empty()) return str;
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

inline std::string read_relative_file(const std::string& filename) {
    std::filesystem::path current_file(__FILE__);
    std::filesystem::path base_dir = current_file.parent_path();
    std::filesystem::path full_path = base_dir / filename;

    std::ifstream ifs(full_path);
    if (! ifs) {
        throw std::runtime_error("Failed to open file: " + full_path.string());
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

inline std::string get_suffix(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();
    // ext 可能是 ".txt"
    if (! ext.empty() && ext[0] == '.') {
        ext.erase(0, 1); // 移除开头的点
    }
    return ext;
}

inline std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;

    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

inline std::string escape_xml(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size() * 1.2); // 预留空间

    for (char c : str) {
        switch (c) {
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '&':
                escaped += "&amp;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

inline bool file_exists(const std::string& filename) {
    return std::filesystem::exists(filename);
}

inline uintmax_t get_file_size(const std::string& filename) {
    if (! file_exists(filename)) return 0;
    return std::filesystem::file_size(filename);
}
} // namespace utils

// 🔥 ===== 颜色方案 =====

class ColorScheme {
  public:
    virtual ~ColorScheme() = default;
    virtual std::string get_color(const std::string& func_name, double heat_ratio = 0.0) const = 0;
    virtual std::string get_name() const = 0;

    // 改进的HSL到RGB转换，支持更精确的颜色控制
    static void hsl_to_rgb(double h, double s, double l, int& r, int& g, int& b) {
        auto hue2rgb = [](double p, double q, double t) {
            if (t < 0) t += 1.0;
            if (t > 1) t -= 1.0;
            if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
            if (t < 1.0 / 2.0) return q;
            if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
            return p;
        };

        h = std::fmod(h, 360.0) / 360.0;
        if (h < 0) h += 1.0;
        s = std::clamp(s, 0.0, 1.0);
        l = std::clamp(l, 0.0, 1.0);

        double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        double p = 2 * l - q;

        auto to255 = [](double v) { return static_cast<int>(std::round(std::clamp(v * 255, 0.0, 255.0))); };

        r = to255(hue2rgb(p, q, h + 1.0 / 3.0));
        g = to255(hue2rgb(p, q, h));
        b = to255(hue2rgb(p, q, h - 1.0 / 3.0));
    }

    // 辅助函数：生成一致的随机化偏移
    static double get_function_hash_offset(const std::string& func_name, double range = 30.0) {
        size_t hash = std::hash<std::string>{}(func_name);
        return ((hash % 1000) / 1000.0 - 0.5) * range; // -range/2 到 +range/2
    }
};

class ClassicHotColorScheme : public ColorScheme {
  public:
    std::string get_color(const std::string& func_name, double heat_ratio = 0.0) const override {
        /*
        冷函数呈现黄绿色（90°附近）
        中等热度呈现橙黄色（30-60°）
        热函数呈现深红色（0°附近）
        */
        /* heat_ratio 越大 → hue 越偏红；底层帧呈黄/橙，越往上越红 */
        double base_hue = 60.0 - 60.0 * std::clamp(heat_ratio, 0.0, 1.0); // 60°(黄) → 0°(红)

        /* 给相同层级的不同函数一点抖动，避免全部纯同色 */
        size_t h = std::hash<std::string>{}(func_name);
        base_hue += (h & 15) * 2; // 0-30° 微抖动

        int r, g, b;
        hsl_to_rgb(base_hue, 0.75, 0.55, r, g, b);
        std::ostringstream oss;
        oss << "rgb(" << r << ',' << g << ',' << b << ')';
        return oss.str();
    }

    std::string get_name() const override {
        return "hot";
    }
};

// 配色方案工厂
class ColorSchemeFactory {
  private:
    // 定义一个映射表，存储可用的配色方案
    using CreatorFunc = std::function<std::unique_ptr<ColorScheme>()>;

    static const std::unordered_map<std::string, CreatorFunc>& get_scheme_map() {
        static const std::unordered_map<std::string, CreatorFunc> scheme_map = {
            {"hot", []() { return std::make_unique<ClassicHotColorScheme>(); }},
            // 如果有新的 ColorScheme，继续加在这里
        };
        return scheme_map;
    }

  public:
    // 创建 ColorScheme
    static std::unique_ptr<ColorScheme> create(const std::string& scheme_name) {
        const auto& map = get_scheme_map();
        auto it = map.find(scheme_name);
        if (it != map.end()) {
            return it->second(); // 调用 lambda，生成实例
        }
        // 未知配色，返回默认 hot
        return std::make_unique<ClassicHotColorScheme>();
    }

    // 列出可用的配色方案
    static std::vector<std::string> get_available_schemes() {
        std::vector<std::string> schemes;
        for (const auto& [name, _] : get_scheme_map()) {
            schemes.push_back(name);
        }
        return schemes;
    }
};

// 🔥 ===== 核心数据结构 =====
// 统计信息结构
struct TreeStats {
    size_t total_nodes = 0;
    size_t leaf_nodes = 0;
    int max_depth = 0;
    size_t total_samples = 0;
    std::vector<size_t> depth_distribution;
};

struct FlameNode {
    std::string name;
    size_t self_count = 0;
    size_t total_count = 0;
    std::map<std::string, std::unique_ptr<FlameNode>> children;
    FlameNode* parent = nullptr; // 父节点指针

    FlameNode() = default;

    explicit FlameNode(const std::string& func_name) : name(func_name) {}

    // 禁用拷贝/移动构造函数
    FlameNode(FlameNode&& other) noexcept = delete;
    FlameNode& operator=(FlameNode&& other) noexcept = delete;
    FlameNode(const FlameNode&) = delete;
    FlameNode& operator=(const FlameNode&) = delete;

    FlameNode* get_or_create_child(const std::string& child_name) {
        auto it = children.find(child_name);
        if (it == children.end()) {
            auto child = std::make_unique<FlameNode>(child_name);
            FlameNode* child_ptr = child.get();
            child_ptr->parent = this; // 设置父指针
            children[child_name] = std::move(child);
            return child_ptr;
        }
        return it->second.get();
    }

    void update_total_count() {
        total_count = self_count;
        for (auto& [name, child] : children) {
            child->update_total_count();
            total_count += child->total_count;
        }
    }

    // 新增：获取路径
    std::vector<std::string> get_path() const {
        std::vector<std::string> path;
        const FlameNode* current = this;
        while (current != nullptr && ! current->name.empty() && current->name != "root") {
            path.push_back(current->name);
            current = current->parent;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    // 新增：计算热度（相对于父节点）
    double get_heat_ratio() const {
        if (! parent || parent->total_count == 0) return 0.0;
        return std::min(1.0, static_cast<double>(total_count) / parent->total_count);
    }

    // 修剪树节点 - 移除小于阈值的节点
    void prune_tree(double threshold) {
        if (total_count == 0) return;

        auto it = children.begin();
        while (it != children.end()) {
            double ratio = static_cast<double>(it->second->total_count) / total_count;
            if (ratio < threshold) {
                it = children.erase(it);
            } else {
                it->second->prune_tree(threshold);
                ++it;
            }
        }
    }

    // 导出为折叠格式
    void export_folded(const std::string& output_file) {
        std::ofstream file(output_file);
        if (! file.is_open()) {
            throw FlameGraphException("Cannot create folded output file: " + output_file);
        }

        this->export_node_folded("", file);
    }

    TreeStats analyze_tree() {
        TreeStats stats;
        analyze_node_recursive(stats, 0);
        return stats;
    }

    std::string to_json_string() const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"name\":\"" << name << "\",";
        oss << "\"value\":" << total_count;

        if (! children.empty()) {
            oss << ",\"children\":[";
            bool first = true;
            for (const auto& [name, child] : children) {
                if (! first) oss << ",";
                oss << child->to_json_string();
                first = false;
            }
            oss << "]";
        }

        oss << "}";
        return oss.str();
    }

  private:
    void export_node_folded(const std::string& stack_prefix, std::ofstream& file) {
        std::string current_stack = stack_prefix;
        if (! name.empty() && name != "root") {
            if (! current_stack.empty()) {
                current_stack += ";";
            }
            current_stack += name;
        }

        // 输出当前节点的自采样
        if (self_count > 0 && ! current_stack.empty()) {
            file << current_stack << " " << self_count << "\n";
        }

        // 递归处理子节点
        for (const auto& [name, child] : children) {
            child->export_node_folded(current_stack, file);
        }
    }

    void analyze_node_recursive(TreeStats& stats, int depth) {
        stats.total_nodes++;
        stats.total_samples += self_count;
        stats.max_depth = std::max(stats.max_depth, depth);

        // 更新深度分布
        if (static_cast<size_t>(depth) >= stats.depth_distribution.size()) {
            stats.depth_distribution.resize(depth + 1, 0);
        }
        stats.depth_distribution[depth]++;

        if (children.empty()) {
            stats.leaf_nodes++;
        } else {
            for (const auto& [name, child] : children) {
                child->analyze_node_recursive(stats, depth + 1);
            }
        }
    }
};

struct FlameGraphConfig {
    std::string title = "Flame Graph";
    int width = 1200;
    int height = 800;
    std::string font_type = "Verdana";
    int font_size = 12;
    std::string colors = "hot";
    bool reverse = true;
    bool inverted = true;
    double min_width = 0.1;
    bool write_folded_file = false;
    bool show_percentages = true;     // 新增：显示百分比
    bool interactive = true;          // 新增：是否生成交互式图表
    int max_depth = 0;                // 新增：最大深度限制，0表示无限制
    double min_heat_threshold = 0.01; // 新增：最小热度阈值

    // 验证配置
    void validate() const {
        if (width <= 0 || height <= 0) {
            throw FlameGraphException("Width and height must be positive");
        }
        if (font_size <= 0) {
            throw FlameGraphException("Font size must be positive");
        }
        if (min_width < 0) {
            throw FlameGraphException("Min width cannot be negative");
        }
    }
};

// 🔥 ===== 堆栈样本数据结构 =====

struct StackSample {
    std::vector<std::string> frames;
    size_t count = 1;
    std::string process_name;
    uint64_t timestamp = 0;
    std::unordered_map<std::string, std::string> metadata; // 新增：元数据

    StackSample() = default;

    StackSample(std::vector<std::string> stack_frames, size_t sample_count = 1)
        : frames(std::move(stack_frames)), count(sample_count) {}

    // 新增：验证样本有效性
    bool is_valid() const {
        return ! frames.empty() && count > 0;
    }
};

// 🔥 ===== 解析器基类和实现 =====

class AbstractStackParser {
  public:
    virtual ~AbstractStackParser() = default;

    virtual std::vector<StackSample> parse(const std::string& filename) = 0;
    virtual std::string get_parser_name() const = 0;

  protected:
    std::string trim(const std::string& str) {
        return utils::trim(str);
    }

    std::vector<std::string> split(const std::string& str, char delimiter) {
        return utils::split(str, delimiter);
    }
};

/**
 * @brief 适配 perf script 收集的堆栈
 */
class PerfScriptParser : public AbstractStackParser {
  public:
    std::vector<StackSample> parse(const std::string& filename) override {
        if (! utils::file_exists(filename)) {
            throw ParseException("File not found: " + filename);
        }

        std::vector<StackSample> samples;
        std::ifstream file(filename);

        if (! file.is_open()) {
            throw ParseException("Cannot open file: " + filename);
        }

        std::string line;
        StackSample current_sample;
        bool reading_stack = false;
        size_t line_count = 0;

        try {
            while (std::getline(file, line)) {
                line_count++;

                line = trim(line);

                if (line.empty()) {
                    if (reading_stack && ! current_sample.frames.empty()) {
                        std::reverse(current_sample.frames.begin(), current_sample.frames.end());
                        if (current_sample.is_valid()) {
                            samples.push_back(std::move(current_sample));
                        }
                        current_sample = StackSample();
                    }
                    reading_stack = false;
                    continue;
                }

                if (! reading_stack && line.find(':') != std::string::npos) {
                    parse_sample_header(line, current_sample);
                    reading_stack = true;
                    continue;
                }

                if (reading_stack) {
                    std::string frame = parse_perf_stack_frame(line);
                    if (! frame.empty()) {
                        current_sample.frames.push_back(frame);
                    }
                }
            }

            // 处理最后一个样本
            if (reading_stack && ! current_sample.frames.empty()) {
                std::reverse(current_sample.frames.begin(), current_sample.frames.end());
                if (current_sample.is_valid()) {
                    samples.push_back(std::move(current_sample));
                }
            }

        } catch (const std::exception& e) {
            throw ParseException("Error parsing line " + std::to_string(line_count) + ": " + e.what());
        }


        if (samples.empty()) {
            throw ParseException("No valid samples found in file");
        }

        return samples;
    }

    std::string get_parser_name() const override {
        return "PerfScriptParser";
    }

  private:
    void parse_sample_header(const std::string& line, StackSample& sample) {
        auto parts = split(line, ' ');
        if (! parts.empty()) {
            sample.process_name = parts[0];
        }

        // 尝试提取时间戳和其他元数据
        std::regex timestamp_regex(R"((\d+\.\d+):)");
        std::smatch match;
        if (std::regex_search(line, match, timestamp_regex)) {
            sample.timestamp = static_cast<uint64_t>(std::stod(match[1].str()) * 1000000); // 转换为微秒
        }
    }

    std::string parse_perf_stack_frame(const std::string& line) {
        std::string trimmed = trim(line);

        size_t first_space = trimmed.find(' ');
        if (first_space == std::string::npos) return "";

        std::string content = trimmed.substr(first_space + 1);
        std::string func_name;
        std::string lib_name;

        size_t paren_start = content.find('(');
        size_t paren_end = content.find(')', paren_start);

        if (paren_start != std::string::npos && paren_end != std::string::npos) {
            lib_name = content.substr(paren_start + 1, paren_end - paren_start - 1);
            func_name = trim(content.substr(0, paren_start));
        } else {
            func_name = content;
        }

        if (func_name.find("[unknown]") != std::string::npos) {
            func_name = "[unknown]";
        } else {
            size_t plus_pos = func_name.find('+');
            if (plus_pos != std::string::npos) {
                func_name = func_name.substr(0, plus_pos);
            }
        }

        if (! lib_name.empty()) {
            size_t last_slash = lib_name.find_last_of('/');
            if (last_slash != std::string::npos) {
                lib_name = lib_name.substr(last_slash + 1);
            }

            // 如果已经是 [xxx]，就不再加括号
            if (! (lib_name.front() == '[' && lib_name.back() == ']')) {
                lib_name = "[" + lib_name + "]";
            }
        }

        if (func_name.empty() || func_name == "[unknown]") {
            return lib_name.empty() ? "[unknown]" : lib_name;
        } else {
            return func_name;
        }
    }
};

/**
 * @brief 适配最常见的“手动采样堆栈”格式
 */
class GenericTextParser : public AbstractStackParser {
  public:
    std::vector<StackSample> parse(const std::string& filename) override {
        if (! utils::file_exists(filename)) {
            throw ParseException("File not found: " + filename);
        }

        std::vector<StackSample> samples;
        std::ifstream file(filename);

        if (! file.is_open()) {
            throw ParseException("Cannot open file: " + filename);
        }

        std::string line;
        std::vector<std::string> current_stack;

        while (std::getline(file, line)) {
            line = trim(line);

            if (line.empty() || line[0] == '#') {
                if (! current_stack.empty()) {
                    samples.emplace_back(std::move(current_stack));
                    current_stack.clear();
                }
                continue;
            }

            current_stack.push_back(line);
        }

        if (! current_stack.empty()) {
            samples.emplace_back(std::move(current_stack));
        }

        return samples;
    }

    std::string get_parser_name() const override {
        return "GenericTextParser";
    }
};

class AutoDetectParser : public AbstractStackParser {
  private:
    std::unique_ptr<AbstractStackParser> actual_parser_;

  public:
    std::vector<StackSample> parse(const std::string& filename) override {
        detect_format(filename);
        if (! actual_parser_) {
            throw ParseException("Unable to detect file format for: " + filename);
        }

        return actual_parser_->parse(filename);
    }

    std::string get_parser_name() const override {
        return actual_parser_ ? "AutoDetect(" + actual_parser_->get_parser_name() + ")" : "AutoDetect(Unknown)";
    }

  private:
    void detect_format(const std::string& filename) {
        std::ifstream file(filename);
        if (! file.is_open()) {
            throw ParseException("Cannot open file: " + filename);
        }

        std::string line;
        int lines_checked = 0;
        bool has_perf_format = false;

        while (std::getline(file, line) && lines_checked < 16) {
            line = trim(line);
            if (line.empty()) continue;

            // 检查perf格式
            if (line.find("cycles:") != std::string::npos || line.find("instructions:") != std::string::npos ||
                (line.find_first_of("0123456789abcdef") == 0 && line.find("(") != std::string::npos)) {
                has_perf_format = true;
                break;
            }

            lines_checked++;
        }

        if (has_perf_format) {
            actual_parser_ = std::make_unique<PerfScriptParser>();
        } else {
            actual_parser_ = std::make_unique<GenericTextParser>();
        }
    }
};

// 🔥 ===== 堆栈折叠器 =====
struct StackCollapseOptions {
    bool merge_kernel_user = false;           // 合并内核和用户空间
    bool ignore_libraries = false;            // 忽略库名
    std::vector<std::string> filter_patterns; // 过滤模式
    size_t min_count_threshold = 1;           // 最小计数阈值
};

class StackCollapser {
  public:
    // 折叠堆栈: 读入样本，生成 folded 文件数据
    std::unordered_map<std::string, size_t> collapse(const std::vector<StackSample>& samples,
                                                     const StackCollapseOptions& options = {}) {
        std::unordered_map<std::string, size_t> folded_stacks;

        for (const auto& sample : samples) {
            if (! sample.is_valid()) continue;

            auto processed_frames = process_frames(sample.frames, options);
            if (processed_frames.empty()) continue;

            std::string folded_stack = join_stack(sample, processed_frames);
            folded_stacks[folded_stack] += sample.count;
        }

        // 应用最小计数阈值
        if (options.min_count_threshold > 1) {
            auto it = folded_stacks.begin();
            while (it != folded_stacks.end()) {
                if (it->second < options.min_count_threshold) {
                    it = folded_stacks.erase(it);
                } else {
                    ++it;
                }
            }
        }

        return folded_stacks;
    }

    // 写 folded 文件
    void write_folded_file(const std::unordered_map<std::string, size_t>& folded_stacks, const std::string& filename) {
        std::ofstream file(filename);
        if (! file.is_open()) {
            throw FlameGraphException("Cannot open folded file: " + filename);
        }

        for (const auto& [stack, count] : folded_stacks) {
            file << stack << " " << count << "\n";
        }
    }

  private:
    std::vector<std::string> process_frames(const std::vector<std::string>& frames, const StackCollapseOptions&) {
        // 🌟 这里只是个最小示例，直接返回原始 frames
        return frames;
    }

    std::string join_stack(const StackSample& sample, const std::vector<std::string>& frames) {
        std::ostringstream oss;
        oss << sample.process_name << ";";
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i > 0) oss << ";";
            oss << frames[i];
        }
        return oss.str();
    }
};

struct FlameGraphBuildOptions {
    int max_depth = 0;             // 最大深度限制
    size_t min_total_count = 1;    // 最小总计数
    bool prune_small_nodes = true; // 修剪小节点
    double prune_threshold = 0.01; // 修剪阈值（百分比）
};

class FlameGraphBuilder {
  public:
    std::unique_ptr<FlameNode> build_tree(const std::unordered_map<std::string, size_t>& folded_stacks,
                                          const FlameGraphBuildOptions& options = {}) {

        auto root = std::make_unique<FlameNode>("root");

        for (const auto& [stack_str, count] : folded_stacks) {
            auto functions = split_stack(stack_str);
            if (functions.empty()) continue;

            // 应用深度限制
            if (options.max_depth > 0 && static_cast<int>(functions.size()) > options.max_depth) {
                functions.resize(options.max_depth);
            }

            FlameNode* current = root.get();
            for (const auto& func : functions) {
                current = current->get_or_create_child(func);
            }

            current->self_count += count;
        }

        root->update_total_count();

        // 修剪小节点
        if (options.prune_small_nodes && root->total_count > 0) {
            root->prune_tree(options.prune_threshold);
        }

        return root;
    }

  private:
    std::vector<std::string> split_stack(const std::string& stack_str) {
        return utils::split(stack_str, ';');
    }
};

class FlameGraphRenderer {
  protected:
    FlameGraphConfig config_;

    explicit FlameGraphRenderer(const FlameGraphConfig& config) : config_(config) {
        config_.validate();
    }

  public:
    virtual void render(const FlameNode& root, const std::string& output_file) = 0;
};

class HtmlFlameGraphRenderer : public FlameGraphRenderer {
  public:
    explicit HtmlFlameGraphRenderer(const FlameGraphConfig& config = {}) : FlameGraphRenderer(config) {}

    void render(const FlameNode& root, const std::string& output_file) {
        std::string d3_css = utils::read_relative_file("d3/d3-flamegraph.css");
        std::string d3_js = utils::read_relative_file("d3/d3.v7.min.js");
        std::string flamegraph_js = utils::read_relative_file("d3/d3-flamegraph.js");
        std::ofstream ofs(output_file);

        ofs << R"(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Flamegraph Viewer</title>
  <style>
)" << d3_css << R"(
  </style>
</head>
<body>
  <h1>Flamegraph</h1>
  <div id="chart"></div>

  <script>
)" << d3_js << R"(
  </script>
  <script>
)" << flamegraph_js
            << R"(
  </script>
  <script>
    const rawData = )"
            << root.to_json_string() << R"(;

    const flameGraph = flamegraph()
      .width(1200)
      .cellHeight(18)
      .transitionDuration(750)
      .minFrameSize(5)
      .selfValue(true)
      .tooltip(true)
      .title("");

    d3.select("#chart")
      .datum(rawData)
      .call(flameGraph);
  </script>
</body>
</html>)";
    }
};

// 🔥 ===== SVG火焰图渲染器  =====
class SvgFlameGraphRenderer : public FlameGraphRenderer {
  private:
    std::ostringstream svg_content_;
    std::unique_ptr<ColorScheme> color_scheme_;
    size_t total_samples_ = 0;
    int max_depth_ = 0;

  public:
    explicit SvgFlameGraphRenderer(const FlameGraphConfig& config = {}) : FlameGraphRenderer(config) {
        setup_color_scheme();
    }

    void render(const FlameNode& root, const std::string& output_file) {
        if (root.total_count == 0) {
            throw RenderException("Root node has no samples to render");
        }

        total_samples_ = root.total_count;
        max_depth_ = calculate_tree_height(root);

        // 动态计算高度：每层16px + 顶部60px + 底部30px
        int calculated_height = max_depth_ * 16 + 90;
        if (calculated_height > config_.height) {
            config_.height = calculated_height;
        }

        svg_content_.str("");
        svg_content_.clear();

        write_svg_header();
        write_background();
        write_title_and_controls();

        // 从底部开始渲染，符合标准火焰图布局
        int base_y = config_.height - 50; // 底部留50px空间
        render_flame_stack(root, 10.0, base_y, config_.width - 20.0, 0);

        write_svg_footer();
        write_to_file(output_file);
    }

  private:
    void setup_color_scheme() {
        color_scheme_ = ColorSchemeFactory::create(config_.colors);
    }

    void write_svg_header() {
        svg_content_ << "<?xml version=\"1.0\" standalone=\"no\"?>\n";
        svg_content_ << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                     << "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
        svg_content_ << "<svg version=\"1.1\" width=\"" << config_.width << "\" height=\"" << config_.height << "\" ";

        if (config_.interactive) {
            svg_content_ << "onload=\"init(evt)\" ";
        }

        svg_content_ << "viewBox=\"0 0 " << config_.width << " " << config_.height
                     << "\" xmlns=\"http://www.w3.org/2000/svg\" "
                     << "xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";

        write_flamegraph_comment();
        write_defs_and_styles();

        if (config_.interactive) {
            write_javascript();
        }
    }

    void write_flamegraph_comment() {
        svg_content_ << "<!-- Flame graph stack visualization. "
                     << "See https://github.com/brendangregg/FlameGraph for latest version, "
                     << "and http://www.brendangregg.com/flamegraphs.html for examples. -->\n";
        svg_content_ << "<!-- NOTES:  -->\n";
    }

    void write_defs_and_styles() {
        svg_content_ << "<defs>\n";
        svg_content_ << "  <linearGradient id=\"background\" y1=\"0\" y2=\"1\" x1=\"0\" x2=\"0\">\n";
        svg_content_ << "    <stop stop-color=\"#eeeeee\" offset=\"5%\" />\n";
        svg_content_ << "    <stop stop-color=\"#eeeeb0\" offset=\"95%\" />\n";
        svg_content_ << "  </linearGradient>\n";
        svg_content_ << "</defs>\n";

        svg_content_ << "<style type=\"text/css\">\n";
        svg_content_ << "  .func_g:hover { stroke:black; stroke-width:0.5; cursor:pointer; }\n";
        svg_content_ << "  .hidden { display: none !important; }\n";
        svg_content_ << "</style>\n";
    }

    void write_javascript() {
        svg_content_ << R"mytag114514(<script type="text/ecmascript">
<![CDATA[
	"use strict";
	var details, searchbtn, unzoombtn, matchedtxt, svg, searching, currentSearchTerm, ignorecase, ignorecaseBtn;
	function init(evt) {
		details = document.getElementById("details").firstChild;
		searchbtn = document.getElementById("search");
		ignorecaseBtn = document.getElementById("ignorecase");
		unzoombtn = document.getElementById("unzoom");
		matchedtxt = document.getElementById("matched");
		svg = document.getElementsByTagName("svg")[0];
		searching = 0;
		currentSearchTerm = null;

		// use GET parameters to restore a flamegraphs state.
		var params = get_params();
		if (params.x && params.y)
			zoom(find_group(document.querySelector('[x="' + params.x + '"][y="' + params.y + '"]')));
                if (params.s) search(params.s);
	}

	// event listeners
	window.addEventListener("click", function(e) {
		var target = find_group(e.target);
		if (target) {
			if (target.nodeName == "a") {
				if (e.ctrlKey === false) return;
				e.preventDefault();
			}
			if (target.classList.contains("parent")) unzoom(true);
			zoom(target);
			if (!document.querySelector('.parent')) {
				// we have basically done a clearzoom so clear the url
				var params = get_params();
				if (params.x) delete params.x;
				if (params.y) delete params.y;
				history.replaceState(null, null, parse_params(params));
				unzoombtn.classList.add("hide");
				return;
			}

			// set parameters for zoom state
			var el = target.querySelector("rect");
			if (el && el.attributes && el.attributes.y && el.attributes._orig_x) {
				var params = get_params()
				params.x = el.attributes._orig_x.value;
				params.y = el.attributes.y.value;
				history.replaceState(null, null, parse_params(params));
			}
		}
		else if (e.target.id == "unzoom") clearzoom();
		else if (e.target.id == "search") search_prompt();
		else if (e.target.id == "ignorecase") toggle_ignorecase();
	}, false)

	// mouse-over for info
	// show
	window.addEventListener("mouseover", function(e) {
		var target = find_group(e.target);
		if (target) details.nodeValue = "$nametype " + g_to_text(target);
	}, false)

	// clear
	window.addEventListener("mouseout", function(e) {
		var target = find_group(e.target);
		if (target) details.nodeValue = ' ';
	}, false)

	// ctrl-F for search
	// ctrl-I to toggle case-sensitive search
	window.addEventListener("keydown",function (e) {
		if (e.keyCode === 114 || (e.ctrlKey && e.keyCode === 70)) {
			e.preventDefault();
			search_prompt();
		}
		else if (e.ctrlKey && e.keyCode === 73) {
			e.preventDefault();
			toggle_ignorecase();
		}
	}, false)

	// functions
	function get_params() {
		var params = {};
		var paramsarr = window.location.search.substr(1).split('&');
		for (var i = 0; i < paramsarr.length; ++i) {
			var tmp = paramsarr[i].split("=");
			if (!tmp[0] || !tmp[1]) continue;
			params[tmp[0]]  = decodeURIComponent(tmp[1]);
		}
		return params;
	}
	function parse_params(params) {
		var uri = "?";
		for (var key in params) {
			uri += key + '=' + encodeURIComponent(params[key]) + '&';
		}
		if (uri.slice(-1) == "&")
			uri = uri.substring(0, uri.length - 1);
		if (uri == '?')
			uri = window.location.href.split('?')[0];
		return uri;
	}
	function find_child(node, selector) {
		var children = node.querySelectorAll(selector);
		if (children.length) return children[0];
	}
	function find_group(node) {
		var parent = node.parentElement;
		if (!parent) return;
		if (parent.id == "frames") return node;
		return find_group(parent);
	}
	function orig_save(e, attr, val) {
		if (e.attributes["_orig_" + attr] != undefined) return;
		if (e.attributes[attr] == undefined) return;
		if (val == undefined) val = e.attributes[attr].value;
		e.setAttribute("_orig_" + attr, val);
	}
	function orig_load(e, attr) {
		if (e.attributes["_orig_"+attr] == undefined) return;
		e.attributes[attr].value = e.attributes["_orig_" + attr].value;
		e.removeAttribute("_orig_"+attr);
	}
	function g_to_text(e) {
		var text = find_child(e, "title").firstChild.nodeValue;
		return (text)
	}
	function g_to_func(e) {
		var func = g_to_text(e);
		// if there's any manipulation we want to do to the function
		// name before it's searched, do it here before returning.
		return (func);
	}
	function update_text(e) {
		var r = find_child(e, "rect");
		var t = find_child(e, "text");
		var w = parseFloat(r.attributes.width.value) -3;
		var txt = find_child(e, "title").textContent.replace(/\\([^(]*\\)\$/,"");
		t.attributes.x.value = parseFloat(r.attributes.x.value) + 3;

		// Smaller than this size won't fit anything
		if (w < 2 * $fontsize * $fontwidth) {
			t.textContent = "";
			return;
		}

		t.textContent = txt;
		var sl = t.getSubStringLength(0, txt.length);
		// check if only whitespace or if we can fit the entire string into width w
		if (/^ *\$/.test(txt) || sl < w)
			return;

		// this isn't perfect, but gives a good starting point
		// and avoids calling getSubStringLength too often
		var start = Math.floor((w/sl) * txt.length);
		for (var x = start; x > 0; x = x-2) {
			if (t.getSubStringLength(0, x + 2) <= w) {
				t.textContent = txt.substring(0, x) + "..";
				return;
			}
		}
		t.textContent = "";
	}

	// zoom
	function zoom_reset(e) {
		if (e.attributes != undefined) {
			orig_load(e, "x");
			orig_load(e, "width");
		}
		if (e.childNodes == undefined) return;
		for (var i = 0, c = e.childNodes; i < c.length; i++) {
			zoom_reset(c[i]);
		}
	}
	function zoom_child(e, x, ratio) {
		if (e.attributes != undefined) {
			if (e.attributes.x != undefined) {
				orig_save(e, "x");
				e.attributes.x.value = (parseFloat(e.attributes.x.value) - x - $xpad) * ratio + $xpad;
				if (e.tagName == "text")
					e.attributes.x.value = find_child(e.parentNode, "rect[x]").attributes.x.value + 3;
			}
			if (e.attributes.width != undefined) {
				orig_save(e, "width");
				e.attributes.width.value = parseFloat(e.attributes.width.value) * ratio;
			}
		}

		if (e.childNodes == undefined) return;
		for (var i = 0, c = e.childNodes; i < c.length; i++) {
			zoom_child(c[i], x - $xpad, ratio);
		}
	}
	function zoom_parent(e) {
		if (e.attributes) {
			if (e.attributes.x != undefined) {
				orig_save(e, "x");
				e.attributes.x.value = $xpad;
			}
			if (e.attributes.width != undefined) {
				orig_save(e, "width");
				e.attributes.width.value = parseInt(svg.width.baseVal.value) - ($xpad * 2);
			}
		}
		if (e.childNodes == undefined) return;
		for (var i = 0, c = e.childNodes; i < c.length; i++) {
			zoom_parent(c[i]);
		}
	}
	function zoom(node) {
		var attr = find_child(node, "rect").attributes;
		var width = parseFloat(attr.width.value);
		var xmin = parseFloat(attr.x.value);
		var xmax = parseFloat(xmin + width);
		var ymin = parseFloat(attr.y.value);
		var ratio = (svg.width.baseVal.value - 2 * $xpad) / width;

		// XXX: Workaround for JavaScript float issues (fix me)
		var fudge = 0.0001;

		unzoombtn.classList.remove("hide");

		var el = document.getElementById("frames").children;
		for (var i = 0; i < el.length; i++) {
			var e = el[i];
			var a = find_child(e, "rect").attributes;
			var ex = parseFloat(a.x.value);
			var ew = parseFloat(a.width.value);
			var upstack;
			// Is it an ancestor
			if ($inverted == 0) {
				upstack = parseFloat(a.y.value) > ymin;
			} else {
				upstack = parseFloat(a.y.value) < ymin;
			}
			if (upstack) {
				// Direct ancestor
				if (ex <= xmin && (ex+ew+fudge) >= xmax) {
					e.classList.add("parent");
					zoom_parent(e);
					update_text(e);
				}
				// not in current path
				else
					e.classList.add("hide");
			}
			// Children maybe
			else {
				// no common path
				if (ex < xmin || ex + fudge >= xmax) {
					e.classList.add("hide");
				}
				else {
					zoom_child(e, xmin, ratio);
					update_text(e);
				}
			}
		}
		search();
	}
	function unzoom(dont_update_text) {
		unzoombtn.classList.add("hide");
		var el = document.getElementById("frames").children;
		for(var i = 0; i < el.length; i++) {
			el[i].classList.remove("parent");
			el[i].classList.remove("hide");
			zoom_reset(el[i]);
			if(!dont_update_text) update_text(el[i]);
		}
		search();
	}
	function clearzoom() {
		unzoom();

		// remove zoom state
		var params = get_params();
		if (params.x) delete params.x;
		if (params.y) delete params.y;
		history.replaceState(null, null, parse_params(params));
	}

	// search
	function toggle_ignorecase() {
		ignorecase = !ignorecase;
		if (ignorecase) {
			ignorecaseBtn.classList.add("show");
		} else {
			ignorecaseBtn.classList.remove("show");
		}
		reset_search();
		search();
	}
	function reset_search() {
		var el = document.querySelectorAll("#frames rect");
		for (var i = 0; i < el.length; i++) {
			orig_load(el[i], "fill")
		}
		var params = get_params();
		delete params.s;
		history.replaceState(null, null, parse_params(params));
	}
	function search_prompt() {
		if (!searching) {
			var term = prompt("Enter a search term (regexp " +
			    "allowed, eg: ^ext4_)"
			    + (ignorecase ? ", ignoring case" : "")
			    + "\\nPress Ctrl-i to toggle case sensitivity", "");
			if (term != null) search(term);
		} else {
			reset_search();
			searching = 0;
			currentSearchTerm = null;
			searchbtn.classList.remove("show");
			searchbtn.firstChild.nodeValue = "Search"
			matchedtxt.classList.add("hide");
			matchedtxt.firstChild.nodeValue = ""
		}
	}
	function search(term) {
		if (term) currentSearchTerm = term;
		if (currentSearchTerm === null) return;

		var re = new RegExp(currentSearchTerm, ignorecase ? 'i' : '');
		var el = document.getElementById("frames").children;
		var matches = new Object();
		var maxwidth = 0;
		for (var i = 0; i < el.length; i++) {
			var e = el[i];
			var func = g_to_func(e);
			var rect = find_child(e, "rect");
			if (func == null || rect == null)
				continue;

			// Save max width. Only works as we have a root frame
			var w = parseFloat(rect.attributes.width.value);
			if (w > maxwidth)
				maxwidth = w;

			if (func.match(re)) {
				// highlight
				var x = parseFloat(rect.attributes.x.value);
				orig_save(rect, "fill");
				rect.attributes.fill.value = "$searchcolor";

				// remember matches
				if (matches[x] == undefined) {
					matches[x] = w;
				} else {
					if (w > matches[x]) {
						// overwrite with parent
						matches[x] = w;
					}
				}
				searching = 1;
			}
		}
		if (!searching)
			return;
		var params = get_params();
		params.s = currentSearchTerm;
		history.replaceState(null, null, parse_params(params));

		searchbtn.classList.add("show");
		searchbtn.firstChild.nodeValue = "Reset Search";

		// calculate percent matched, excluding vertical overlap
		var count = 0;
		var lastx = -1;
		var lastw = 0;
		var keys = Array();
		for (k in matches) {
			if (matches.hasOwnProperty(k))
				keys.push(k);
		}
		// sort the matched frames by their x location
		// ascending, then width descending
		keys.sort(function(a, b){
			return a - b;
		});
		// Step through frames saving only the biggest bottom-up frames
		// thanks to the sort order. This relies on the tree property
		// where children are always smaller than their parents.
		var fudge = 0.0001;	// JavaScript floating point
		for (var k in keys) {
			var x = parseFloat(keys[k]);
			var w = matches[keys[k]];
			if (x >= lastx + lastw - fudge) {
				count += w;
				lastx = x;
				lastw = w;
			}
		}
		// display matched percent
		matchedtxt.classList.remove("hide");
		var pct = 100 * count / maxwidth;
		if (pct != 100) pct = pct.toFixed(1)
		matchedtxt.firstChild.nodeValue = "Matched: " + pct + "%";
	}
]]>
</script>)mytag114514";
    }

    void write_background() {
        svg_content_ << "<rect x=\"0.0\" y=\"0\" width=\"" << config_.width << ".0\" height=\"" << config_.height
                     << ".0\" fill=\"url(#background)\" />\n";
    }

    void write_title_and_controls() {
        // 主标题
        svg_content_ << "<text text-anchor=\"middle\" x=\"" << (config_.width / 2)
                     << "\" y=\"24\" font-size=\"17\" font-family=\"Verdana\" fill=\"rgb(0,0,0)\">"
                     << utils::escape_xml(config_.title) << "</text>\n";

        if (config_.interactive) {
            // 详情显示区域
            svg_content_ << "<text text-anchor=\"\" x=\"10.00\" y=\"" << (config_.height - 17)
                         << "\" font-size=\"12\" font-family=\"Verdana\" fill=\"rgb(0,0,0)\" id=\"details\"> </text>\n";

            // 重置缩放按钮
            svg_content_ << "<text text-anchor=\"\" x=\"10.00\" y=\"24\" font-size=\"12\" "
                         << "font-family=\"Verdana\" fill=\"rgb(0,0,0)\" id=\"unzoom\" "
                         << "onclick=\"unzoom()\" style=\"opacity:0.0;cursor:pointer\">Reset Zoom</text>\n";

            // 搜索按钮
            svg_content_ << "<text text-anchor=\"\" x=\"" << (config_.width - 110)
                         << "\" y=\"24\" font-size=\"12\" font-family=\"Verdana\" fill=\"rgb(0,0,0)\" "
                         << "id=\"search\" onmouseover=\"searchover()\" onmouseout=\"searchout()\" "
                         << "onclick=\"search_prompt()\" style=\"opacity:0.1;cursor:pointer\">Search</text>\n";

            // 匹配结果显示
            svg_content_ << "<text text-anchor=\"\" x=\"" << (config_.width - 110) << "\" y=\"" << (config_.height - 17)
                         << "\" font-size=\"12\" font-family=\"Verdana\" fill=\"rgb(0,0,0)\" "
                         << "id=\"matched\"> </text>\n";
        }
    }

    void render_flame_stack(const FlameNode& node, double x, double base_y, double width, int depth) {
        // 使用更严格的浮点比较
        const double epsilon = 1e-9;
        if (width < config_.min_width + epsilon) return;
        if (config_.max_depth > 0 && depth >= config_.max_depth) return;

        // 计算当前节点的Y坐标（从底部往上）
        double current_y = base_y - (depth * 16);

        // 渲染当前节点（跳过根节点的渲染）
        if (depth > 0) {
            render_rect_element(node, x, current_y, width);
        }

        // 渲染子节点
        if (! node.children.empty()) {
            double child_x = x;

            for (const auto& [child_name, child] : node.children) {
                if (node.total_count == 0) continue;

                double child_width = width * (static_cast<double>(child->total_count) / node.total_count);

                if (child_width >= config_.min_width + epsilon) {
                    render_flame_stack(*child, child_x, base_y, child_width, depth + 1);
                }

                child_x += child_width;
            }
        }
    }

    void render_rect_element(const FlameNode& node, double x, double y, double width) {
        const int rect_height = 15;

        // 生成颜色
        std::string color = color_scheme_->get_color(node.name, node.get_heat_ratio());

        // 开始group元素
        svg_content_ << "<g class=\"func_g\"";

        if (config_.interactive) {
            svg_content_ << " onmouseover=\"s(this)\" onmouseout=\"c()\" onclick=\"zoom(this)\"";
        }

        svg_content_ << ">\n";

        // 构建tooltip内容
        std::string tooltip = build_tooltip_text(node);
        svg_content_ << "<title>" << utils::escape_xml(tooltip) << "</title>";

        // 渲染矩形 - 使用更高精度
        svg_content_ << "<rect x=\"" << std::fixed << std::setprecision(2) << x << "\" y=\"" << static_cast<int>(y)
                     << "\" width=\"" << std::setprecision(2) << width << "\" height=\"" << rect_height << ".0\" "
                     << "fill=\"" << color << "\" rx=\"2\" ry=\"2\" />\n";

        // 渲染文本（如果空间够大）
        if (width > 20) { // 只有足够宽时才显示文本
            std::string display_text = get_display_text(node.name, width);
            if (! display_text.empty()) {
                svg_content_ << "<text text-anchor=\"\" x=\"" << std::setprecision(2) << (x + 3) << "\" y=\""
                             << std::setprecision(1) << (y + 11.5)
                             << "\" font-size=\"12\" font-family=\"Verdana\" fill=\"rgb(0,0,0)\">"
                             << utils::escape_xml(display_text) << "</text>\n";
            }
        }

        svg_content_ << "</g>\n";
    }

    std::string build_tooltip_text(const FlameNode& node) {
        std::ostringstream tooltip;
        tooltip << node.name;

        if (config_.show_percentages && total_samples_ > 0) {
            double percentage = (static_cast<double>(node.total_count) / total_samples_) * 100.0;
            tooltip << " (" << node.total_count << " samples, " << std::fixed << std::setprecision(2) << percentage
                    << "%)";
        } else {
            tooltip << " (" << node.total_count << " samples)";
        }

        return tooltip.str();
    }

    std::string get_display_text(const std::string& text, double width) {
        // 基于宽度估算可显示的字符数（假设平均字符宽度约为7像素）
        int max_chars = static_cast<int>((width - 6) / 7); // 减去padding

        if (max_chars < 4) return ""; // 太窄不显示

        if (static_cast<int>(text.length()) <= max_chars) {
            return text;
        }

        // 智能截断：优先保留函数名
        size_t last_colon = text.find_last_of(':');
        size_t last_slash = text.find_last_of('/');
        size_t last_space = text.find_last_of(' ');

        size_t split_pos = std::max({last_colon, last_slash, last_space});

        if (split_pos != std::string::npos && split_pos + 1 < text.length()) {
            std::string suffix = text.substr(split_pos + 1);
            if (static_cast<int>(suffix.length()) + 3 <= max_chars) {
                return ".." + suffix;
            }
        }

        // 简单截断
        return text.substr(0, max_chars - 2) + "..";
    }

    int calculate_tree_height(const FlameNode& node) {
        int max_depth = 0;
        calculate_depth_recursive(node, 0, max_depth);
        return max_depth;
    }

    void calculate_depth_recursive(const FlameNode& node, int current_depth, int& max_depth) {
        max_depth = std::max(max_depth, current_depth);
        for (const auto& [name, child] : node.children) {
            calculate_depth_recursive(*child, current_depth + 1, max_depth);
        }
    }

    void write_svg_footer() {
        svg_content_ << "</svg>\n";
    }

    void write_to_file(const std::string& output_file) {
        std::ofstream file(output_file);
        if (! file.is_open()) {
            throw RenderException("Cannot create SVG file: " + output_file);
        }

        file << svg_content_.str();

        if (! file.good()) {
            throw RenderException("Error writing to SVG file: " + output_file);
        }
    }
};

class FlameGraphRendererFactory {
    using CreatorFunc = std::function<std::unique_ptr<FlameGraphRenderer>()>;

    static const std::unordered_map<std::string, CreatorFunc>& get_render_map() {
        static const std::unordered_map<std::string, CreatorFunc> render_map = {
            { "svg",  []() { return std::make_unique<SvgFlameGraphRenderer>(); }},
            {"html", []() { return std::make_unique<HtmlFlameGraphRenderer>(); }},
        };
        return render_map;
    }

  public:
    static std::unique_ptr<FlameGraphRenderer> create(const std::string& render_suffix) {
        const auto& map = get_render_map();
        auto it = map.find(render_suffix);
        if (it != map.end()) {
            return it->second(); // 调用 lambda，生成实例
        }
        // 未知默认返回 html
        return std::make_unique<HtmlFlameGraphRenderer>();
    }
};

// 🔥 ===== 主入口类 =====
class FlameGraphGenerator {
  private:
    FlameGraphConfig config_;

  public:
    explicit FlameGraphGenerator(const FlameGraphConfig& config = {}) : config_(config) {
        config_.validate();
    }

    void generate_from_raw(const std::string& raw_file, const std::string& out_file) {
        auto parser = std::make_unique<AutoDetectParser>();
        StackCollapser collapser;
        FlameGraphBuilder builder;
        auto renderer = FlameGraphRendererFactory::create(utils::get_suffix(out_file));

        try {
            // 解析原始数据
            auto samples = parser->parse(raw_file);

            if (samples.empty()) {
                throw FlameGraphException("No valid samples found in input file");
            }

            std::cout << "Parsed " << samples.size() << " samples using " << parser->get_parser_name() << "\n";

            // 折叠堆栈
            StackCollapseOptions collapse_opts;
            // 可以根据配置设置折叠选项
            auto folded = collapser.collapse(samples, collapse_opts);

            if (folded.empty()) {
                throw FlameGraphException("No stacks remained after collapsing");
            }

            if (config_.write_folded_file) {
                collapser.write_folded_file(folded, raw_file + ".collapse");
            }

            std::cout << "Collapsed to " << folded.size() << " unique stacks\n";

            // 构建树
            FlameGraphBuildOptions build_opts;
            build_opts.max_depth = config_.max_depth;
            build_opts.prune_threshold = config_.min_heat_threshold;

            auto tree = builder.build_tree(folded, build_opts);

            if (tree->total_count == 0) {
                throw FlameGraphException("Tree has no samples");
            }

            std::cout << "Built tree with " << tree->total_count << " total samples\n";

            renderer->render(*tree, out_file);

            std::cout << "Generated interactive flame graph: " << out_file << "\n";

        } catch (const std::exception& e) {
            throw FlameGraphException("Generation failed: " + std::string(e.what()));
        }
    }

    void set_config(const FlameGraphConfig& config) {
        config.validate();
        config_ = config;
    }

    const FlameGraphConfig& get_config() const {
        return config_;
    }
};
} // namespace flamegraph
