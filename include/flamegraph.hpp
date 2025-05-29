#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
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

namespace {
inline std::string_view trim(std::string_view str) {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {}; // empty view
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    // substr(pos, len)， len=last-first+1
    return str.substr(first, last - first + 1);
}

inline std::string read_relative_file(std::string_view filename) {
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

inline std::string_view file_suffix(std::string_view path) {
    // 找到最后一个点
    size_t last_dot = path.find_last_of('.');
    if (last_dot == std::string_view::npos || last_dot == path.size() - 1) {
        return {}; // 没有后缀
    }

    // 还要确保点在最后一个路径分隔符之后, 兼容 Windows 和 Unix
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string_view::npos && last_dot < last_slash) {
        return {};
    }

    // 返回点之后的部分
    return path.substr(last_dot + 1);
}

inline std::vector<std::string_view> split(std::string_view str, char delimiter) {
    std::vector<std::string_view> tokens;

    size_t start = 0;
    while (true) {
        size_t end = str.find(delimiter, start);
        if (end == std::string_view::npos) {
            tokens.emplace_back(str.substr(start));
            break;
        }
        tokens.emplace_back(str.substr(start, end - start));
        start = end + 1;
    }

    return tokens;
}

inline std::string escape_xml(std::string_view str) {
    std::string escaped;
    size_t reserve_size = str.size() + (str.size() / 5); // 预留 0.2 的空间
    escaped.reserve(reserve_size);

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

inline bool file_exists(std::string_view filename) {
    return std::filesystem::exists(filename);
}

inline uintmax_t get_file_size(std::string_view filename) {
    if (! file_exists(filename)) return 0;
    return std::filesystem::file_size(filename);
}
} // namespace

// 🔥 ===== 颜色方案 =====

class ColorScheme {
  public:
    virtual ~ColorScheme() = default;
    virtual std::string get_color(std::string_view func_name, double heat_ratio = 0.0) const = 0;
    virtual std::string_view get_name() const = 0;

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
    static double get_function_hash_offset(std::string_view func_name, double range = 30.0) {
        size_t hash = std::hash<std::string_view>{}(func_name);
        // ratio ∈ [0.0, 0.999]
        double ratio = static_cast<double>(hash % 1000) / 1000.0;
        return (ratio - 0.5) * range; // -range/2 到 +range/2
    }
};

class ClassicHotColorScheme : public ColorScheme {
  public:
    std::string get_color(std::string_view func_name, double heat_ratio = 0.0) const override {
        /* heat_ratio 越大 → hue 越偏红；底层帧呈黄/橙，越往上越红 */
        double hue = 60.0 - 60.0 * std::clamp(heat_ratio, 0.0, 1.0); // 60° → 0°
        hue += get_function_hash_offset(func_name, 30.0);
        double saturation = 1.0;
        double lightness = 0.5; // 中等亮度，经典高饱和色彩
        int r, g, b;
        hsl_to_rgb(hue, saturation, lightness, r, g, b);

        std::ostringstream oss;
        oss << "rgb(" << r << ',' << g << ',' << b << ')';
        return oss.str();
    }

    std::string_view get_name() const override {
        return "hot";
    }
};

// 配色方案工厂
class ColorSchemeFactory {
  private:
    // 定义一个映射表，存储可用的配色方案
    using CreatorFunc = std::function<std::unique_ptr<ColorScheme>()>;

    static const std::unordered_map<std::string_view, CreatorFunc>& get_scheme_map() {
        static const std::unordered_map<std::string_view, CreatorFunc> scheme_map = {
            {"hot", []() { return std::make_unique<ClassicHotColorScheme>(); }},
            // 如果有新的 ColorScheme，继续加在这里
        };
        return scheme_map;
    }

  public:
    // 创建 ColorScheme
    static std::unique_ptr<ColorScheme> create(std::string_view scheme_name) {
        const auto& map = get_scheme_map();
        auto it = map.find(scheme_name);
        if (it != map.end()) {
            return it->second(); // 调用 lambda，生成实例
        }
        // 未知配色，返回默认 hot
        return std::make_unique<ClassicHotColorScheme>();
    }

    // 列出可用的配色方案
    static const std::vector<std::string_view> get_available_schemes() {
        std::vector<std::string_view> schemes;
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
    std::map<std::string, std::unique_ptr<FlameNode>, std::less<>> children;
    FlameNode* parent = nullptr; // 父节点指针

    FlameNode() = default;

    explicit FlameNode(std::string_view func_name) : name(func_name) {}

    // 禁用拷贝/移动构造函数
    FlameNode(FlameNode&& other) noexcept = delete;
    FlameNode& operator=(FlameNode&& other) noexcept = delete;
    FlameNode(const FlameNode&) = delete;
    FlameNode& operator=(const FlameNode&) = delete;

    FlameNode* get_or_create_child(std::string_view child_name) {
        auto it = children.find(child_name);
        if (it == children.end()) {
            auto child = std::make_unique<FlameNode>(child_name);
            FlameNode* child_ptr = child.get();
            child_ptr->parent = this; // 设置父指针
            children[std::string(child_name)] = std::move(child);
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
        return std::min(1.0, static_cast<double>(total_count) / static_cast<double>(parent->total_count));
    }

    // 修剪树节点 - 移除小于阈值的节点
    void prune_tree(double threshold) {
        if (total_count == 0) return;

        auto it = children.begin();
        while (it != children.end()) {
            double ratio = static_cast<double>(it->second->total_count) / static_cast<double>(total_count);
            if (ratio < threshold) {
                it = children.erase(it);
            } else {
                it->second->prune_tree(threshold);
                ++it;
            }
        }
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
    // 标题和说明
    std::string title = "Flame Graph";
    std::string subtitle = "subtitle"; // 默认无副标题

    // 图像尺寸
    int width = 1200;      // 标准宽度
    int height = 0;        // 0 表示自动计算（根据堆栈深度）
    int frame_height = 16; // 每个框架的高度

    // 边距
    int xpad = 10; // 左右边距

    // 字体设置
    std::string font_type = "Verdana"; // 标准字体
    int font_size = 12;                // 标准字体大小
    double font_width = 0.6;           // 字符宽度相对于 font_size 的比例

    // 颜色设置
    std::string colors = "hot";                  // 默认配色方案（hot, mem, io, java 等）
    std::string bgcolor1 = "#eeeeee";            // 背景渐变开始颜色
    std::string bgcolor2 = "#eeeeb0";            // 背景渐变结束颜色
    std::string search_color = "rgb(230,0,230)"; // 搜索高亮颜色

    // 文本标签
    std::string name_type = "Function:"; // 函数名前缀
    std::string count_name = "samples";  // 计数单位名称
    std::string notes = "";              // SVG 内嵌注释

    // 布局选项
    bool reverse = false;  // false: 正常的调用栈顺序
    bool inverted = false; // false: 火焰图, true: 冰柱图

    // 过滤和显示选项
    double min_width = 0.1;          // 最小像素宽度（小于此值的框架不显示）
    int max_depth = 0;               // 0 表示无限制
    double min_heat_threshold = 0.0; // 0 表示显示所有

    // 功能开关
    bool interactive = true;        // 生成交互式 SVG
    bool write_folded_file = false; // 是否同时输出折叠格式文件

    // 验证配置
    void validate() const {
        if (width <= 0) {
            throw FlameGraphException("Width must be positive");
        }
        // height 可以为 0（自动计算）
        if (font_size <= 0) {
            throw FlameGraphException("Font size must be positive");
        }
        if (min_width < 0) {
            throw FlameGraphException("Min width cannot be negative");
        }
        if (font_width <= 0 || font_width > 1) {
            throw FlameGraphException("Font width must be between 0 and 1");
        }
        if (xpad < 0) {
            throw FlameGraphException("Padding cannot be negative");
        }

        // 如果没有设置 frame_height，添加验证
        if (frame_height <= 0) {
            throw FlameGraphException("Frame height must be positive");
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

    virtual std::vector<StackSample> parse(std::string_view filename) = 0;
    virtual std::string_view get_parser_name() const = 0;
};

/**
 * @brief 适配 perf script 收集的堆栈
 */
class PerfScriptParser : public AbstractStackParser {
  public:
    std::vector<StackSample> parse(std::string_view filename) override {
        if (! file_exists(filename)) {
            throw ParseException(std::string("File not found: ") + filename.data());
        }

        std::vector<StackSample> samples;
        std::ifstream file(filename.data());

        if (! file.is_open()) {
            throw ParseException(std::string("Cannot open file: ") + filename.data());
        }

        std::string line;
        std::string_view line_view;
        StackSample current_sample;
        bool reading_stack = false;
        size_t line_count = 0;

        try {
            while (std::getline(file, line)) {
                line_count++;

                line_view = trim(line);

                if (line_view.empty()) {
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

                if (! reading_stack && line_view.find(':') != std::string::npos) {
                    parse_sample_header(line_view, current_sample);
                    reading_stack = true;
                    continue;
                }

                if (reading_stack) {
                    std::string_view frame = parse_perf_stack_frame(line_view);
                    if (! frame.empty()) {
                        // 这里需要 string_view 转 string, 从而让 vector 拥有所有权
                        current_sample.frames.push_back(std::string(frame));
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

    std::string_view get_parser_name() const override {
        return "PerfScriptParser";
    }

  private:
    void parse_sample_header(std::string_view line_view, StackSample& sample) {
        auto parts = split(line_view, ' ');
        if (! parts.empty()) {
            sample.process_name = parts[0];
        }

        // 尝试提取时间戳和其他元数据
        std::regex timestamp_regex(R"((\d+\.\d+):)");
        std::cmatch match;
        if (std::regex_search(line_view.begin(), line_view.end(), match, timestamp_regex)) {
            sample.timestamp = static_cast<uint64_t>(std::stod(match[1].str()) * 1000000); // 转换为微秒
        }
    }

    std::string_view parse_perf_stack_frame(std::string_view line_view) {
        std::string_view trimmed = trim(line_view);

        size_t first_space = trimmed.find(' ');
        if (first_space == std::string::npos) return "";

        std::string_view content = trimmed.substr(first_space + 1);
        std::string_view func_name;
        std::string_view lib_name;

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
                lib_name = std::string("[") + lib_name.data() + "]";
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
    std::vector<StackSample> parse(std::string_view filename) override {
        if (! file_exists(filename)) {
            throw ParseException(std::string("File not found: ") + filename.data());
        }

        std::vector<StackSample> samples;
        std::ifstream file(filename.data());

        if (! file.is_open()) {
            throw ParseException(std::string("Cannot open file: ") + filename.data());
        }

        std::string line;
        std::string_view line_view;
        std::vector<std::string> current_stack;

        while (std::getline(file, line)) {
            line_view = trim(line);

            if (line_view.empty() || line_view[0] == '#') {
                if (! current_stack.empty()) {
                    samples.emplace_back(std::move(current_stack));
                    current_stack.clear();
                }
                continue;
            }

            // 转移所有权
            current_stack.push_back(std::string(line_view));
        }

        if (! current_stack.empty()) {
            samples.emplace_back(std::move(current_stack));
        }

        return samples;
    }

    std::string_view get_parser_name() const override {
        return "GenericTextParser";
    }
};

class AutoDetectParser : public AbstractStackParser {
  private:
    std::unique_ptr<AbstractStackParser> actual_parser_;

  public:
    std::vector<StackSample> parse(std::string_view filename) override {
        detect_format(filename);
        if (! actual_parser_) {
            throw ParseException(std::string("Unable to detect file format for: ") + filename.data());
        }

        return actual_parser_->parse(filename);
    }

    std::string_view get_parser_name() const override {
        return "AutoDetectParser";
    }

    std::string get_using_parser() const {
        std::ostringstream oss;
        if (actual_parser_) {
            oss << "AutoDetect(" << actual_parser_->get_parser_name().data() << ")";
        } else {
            oss << "AutoDetect(Unknown)";
        }
        return oss.str();
    }

  private:
    void detect_format(std::string_view filename) {
        std::ifstream file(filename.data());
        if (! file.is_open()) {
            throw ParseException(std::string("Cannot open file: ") + filename.data());
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
        // 会 join_stack 生成新 string 所以 key 是 std::string
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
    void write_folded_file(const std::unordered_map<std::string, size_t>& folded_stacks, std::string_view filename) {
        std::ofstream file(filename.data());
        if (! file.is_open()) {
            throw FlameGraphException(std::string("Cannot open folded file: ") + filename.data());
        }

        for (const auto& [stack, count] : folded_stacks) {
            file << stack << " " << count << "\n";
        }
    }

  private:
    std::vector<std::string> process_frames(const std::vector<std::string>& frames, const StackCollapseOptions&) {
        // 这里只是个最小示例，直接返回原始 frames
        return frames;
    }

    std::string join_stack(const StackSample& sample, const std::vector<std::string>& frames) {
        std::ostringstream oss;
        oss << (sample.process_name.empty() ? "UNKNOWN PROCESS" : sample.process_name) << ";";
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i > 0) oss << ";";
            oss << frames[i];
        }
        return oss.str();
    }
};

struct FlameGraphBuildOptions {
    int max_depth = 0;              // 最大深度限制
    size_t min_total_count = 1;     // 最小总计数
    bool prune_small_nodes = false; // 修剪小节点
    double prune_threshold = 0.01;  // 修剪阈值（百分比）
};

class FlameGraphBuilder {
  public:
    std::unique_ptr<FlameNode> build_tree(const std::unordered_map<std::string, size_t>& folded_stacks,
                                          const FlameGraphBuildOptions& options = {}) {

        auto root = std::make_unique<FlameNode>("root");

        for (const auto& [stack_str, count] : folded_stacks) {
            auto functions = split(stack_str, ';');
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
};

class FlameGraphRenderer {
  protected:
    FlameGraphConfig config_;

    explicit FlameGraphRenderer(const FlameGraphConfig& config) : config_(config) {
        config_.validate();
    }

  public:
    virtual void render(const FlameNode& root, std::string_view output_file) = 0;
    virtual ~FlameGraphRenderer() = default;
};

class HtmlFlameGraphRenderer : public FlameGraphRenderer {
  public:
    explicit HtmlFlameGraphRenderer(const FlameGraphConfig& config = {}) : FlameGraphRenderer(config) {}

    void render(const FlameNode& root, std::string_view output_file) override {
        std::string d3_css = read_relative_file("d3/d3-flamegraph.css");
        std::string d3_js = read_relative_file("d3/d3.v7.min.js");
        std::string flamegraph_js = read_relative_file("d3/d3-flamegraph.js");
        std::ofstream ofs(output_file.data());

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
#include "embed/flamegraph_js_embed.hpp" // FLAMEGRAPH_JS 变量可用

    std::ostringstream svg_content_;
    std::unique_ptr<ColorScheme> color_scheme_;
    size_t total_samples_ = 0;
    int max_depth_ = 0;

  public:
    explicit SvgFlameGraphRenderer(const FlameGraphConfig& config = {}) : FlameGraphRenderer(config) {
        setup_color_scheme();
    }

    void render(const FlameNode& root, std::string_view output_file) override {
        if (root.total_count == 0) {
            throw RenderException("Root node has no samples to render");
        }

        total_samples_ = root.total_count;
        max_depth_ = calculate_tree_height(root);

        // 计算图像高度
        int imageheight = calculate_image_height();

        // 清空内容
        svg_content_.str("");
        svg_content_.clear();

        // 写入 SVG
        write_svg_header(imageheight);
        write_svg_defs();
        write_svg_style();
        write_svg_script();
        write_svg_background(imageheight);
        write_svg_controls(imageheight);

        // 写入火焰图框架
        svg_content_ << "<g id=\"frames\">\n";

        if (config_.inverted) {
            // Icicle 图（倒置）
            render_frames_icicle(root);
        } else {
            // 标准火焰图
            render_frames_flamegraph(root);
        }

        svg_content_ << "</g>\n";
        svg_content_ << "</svg>\n";

        // 写入文件
        write_to_file(output_file);
    }

  private:
    void setup_color_scheme() {
        color_scheme_ = ColorSchemeFactory::create(config_.colors);
    }

    int calculate_image_height() const {
        int ypad1 = config_.font_size * 3;                                // 顶部空间（标题）
        int ypad2 = config_.font_size * 2 + 10;                           // 底部空间（标签）
        int ypad3 = config_.subtitle.empty() ? 0 : config_.font_size * 2; // 副标题空间

        return (max_depth_ + 1) * config_.frame_height + ypad1 + ypad2 + ypad3;
    }

    void write_svg_header(int imageheight) {
        svg_content_ << "<?xml version=\"1.0\" standalone=\"no\"?>\n";
        svg_content_ << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                     << "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
        svg_content_ << "<svg version=\"1.1\" "
                     << "width=\"" << config_.width << "\" "
                     << "height=\"" << imageheight << "\" "
                     << "onload=\"init(evt)\" "
                     << "viewBox=\"0 0 " << config_.width << " " << imageheight << "\" "
                     << "xmlns=\"http://www.w3.org/2000/svg\" "
                     << "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                     << "xmlns:fg=\"http://github.com/jonhoo/inferno\">\n";

        svg_content_ << "<!-- Flame graph stack visualization. "
                     << "See https://github.com/brendangregg/FlameGraph for latest version, "
                     << "and http://www.brendangregg.com/flamegraphs.html for examples. -->\n";
        svg_content_ << "<!-- NOTES: " << escape_xml(config_.notes) << " -->\n";
    }

    void write_svg_defs() {
        svg_content_ << "<defs>\n";
        svg_content_ << "  <linearGradient id=\"background\" y1=\"0\" y2=\"1\" x1=\"0\" x2=\"0\">\n";
        svg_content_ << "    <stop stop-color=\"" << config_.bgcolor1 << "\" offset=\"5%\" />\n";
        svg_content_ << "    <stop stop-color=\"" << config_.bgcolor2 << "\" offset=\"95%\" />\n";
        svg_content_ << "  </linearGradient>\n";
        svg_content_ << "</defs>\n";
    }

    void write_svg_style() {
        int title_size = config_.font_size + 5;

        svg_content_ << "<style type=\"text/css\">\n";
        svg_content_ << "  text { font-family:" << config_.font_type << "; font-size:" << config_.font_size
                     << "px; fill:black; }\n";
        svg_content_ << "  #search, #ignorecase { opacity:0.1; cursor:pointer; }\n";
        svg_content_ << "  #search:hover, #search.show, #ignorecase:hover, #ignorecase.show { opacity:1; }\n";
        svg_content_ << "  #subtitle { text-anchor:middle; font-color:rgb(160,160,160); }\n";
        svg_content_ << "  #title { text-anchor:middle; font-size:" << title_size << "px}\n";
        svg_content_ << "  #unzoom { cursor:pointer; }\n";
        svg_content_ << "  #frames > *:hover { stroke:black; stroke-width:0.5; cursor:pointer; }\n";
        svg_content_ << "  .hide { display:none; }\n";
        svg_content_ << "  .parent { opacity:0.5; }\n";
        svg_content_ << "</style>\n";
    }

    void write_svg_script() {
        svg_content_ << "<script type=\"text/ecmascript\">\n<![CDATA[\n";

        // 首先声明 use strict
        svg_content_ << "\"use strict\";\n";

        // 声明全局变量（只声明一次）
        svg_content_
            << "var details, searchbtn, unzoombtn, matchedtxt, svg, searching, currentSearchTerm, ignorecase, ignorecaseBtn;\n";

        // 注入配置变量（作为赋值，不是新的声明）
        svg_content_ << "var fontsize = " << config_.font_size << ";\n";
        svg_content_ << "var fontwidth = " << std::fixed << std::setprecision(2) << config_.font_width << ";\n";
        svg_content_ << "var xpad = " << config_.xpad << ";\n";
        svg_content_ << "var inverted = " << (config_.inverted ? "true" : "false") << ";\n";
        svg_content_ << "var searchcolor = '" << config_.search_color << "';\n";
        svg_content_ << "var nametype = '" << escape_js(config_.name_type) << "';\n\n";

        // 注入 JavaScript 代码
        svg_content_ << FLAMEGRAPH_JS;

        svg_content_ << "]]>\n</script>\n";
    }

    void write_svg_background(int imageheight) {
        svg_content_ << "<rect x=\"0.0\" y=\"0\" width=\"" << config_.width << "\" height=\"" << imageheight
                     << "\" fill=\"url(#background)\" />\n";
    }

    void write_svg_controls(int imageheight) {
        int ypad2 = config_.font_size * 2 + 10;

        // 标题
        svg_content_ << "<text id=\"title\" x=\"" << (config_.width / 2) << "\" y=\"" << (config_.font_size * 2)
                     << "\">" << escape_xml(config_.title) << "</text>\n";

        // 副标题
        if (! config_.subtitle.empty()) {
            svg_content_ << "<text id=\"subtitle\" x=\"" << (config_.width / 2) << "\" y=\"" << (config_.font_size * 4)
                         << "\">" << escape_xml(config_.subtitle) << "</text>\n";
        }

        // 详情文本
        svg_content_ << "<text id=\"details\" x=\"" << config_.xpad << "\" y=\"" << (imageheight - ypad2 / 2)
                     << "\"> </text>\n";

        // 重置缩放按钮
        svg_content_ << "<text id=\"unzoom\" x=\"" << config_.xpad << "\" y=\"" << (config_.font_size * 2)
                     << "\" class=\"hide\">Reset Zoom</text>\n";

        // 搜索按钮
        svg_content_ << "<text id=\"search\" x=\"" << (config_.width - config_.xpad - 100) << "\" y=\""
                     << (config_.font_size * 2) << "\">Search</text>\n";

        // 忽略大小写按钮
        svg_content_ << "<text id=\"ignorecase\" x=\"" << (config_.width - config_.xpad - 16) << "\" y=\""
                     << (config_.font_size * 2) << "\">ic</text>\n";

        // 匹配文本
        svg_content_ << "<text id=\"matched\" x=\"" << (config_.width - config_.xpad - 100) << "\" y=\""
                     << (imageheight - ypad2 / 2) << "\"> </text>\n";
    }

    void render_frames_flamegraph(const FlameNode& root) {
        int ypad = config_.font_size * 2 + 10;

        double width_per_sample = (config_.width - 2.0 * config_.xpad) / static_cast<double>(total_samples_);

        // 渲染根节点
        int imageheight = calculate_image_height();
        double y = imageheight - ypad - config_.frame_height;

        render_frame(root, config_.xpad, y, config_.width - 2 * config_.xpad, "", 0);

        // 递归渲染子节点
        render_children_flamegraph(root, config_.xpad, y, 1, width_per_sample);
    }

    void render_frames_icicle(const FlameNode& root) {
        int ypad1 = config_.font_size * 3;
        int ypad3 = config_.subtitle.empty() ? 0 : config_.font_size * 2;

        double width_per_sample = (config_.width - 2.0 * config_.xpad) / static_cast<double>(total_samples_);
        double y = ypad1 + ypad3;

        // 渲染根节点
        render_frame(root, config_.xpad, y, config_.width - 2 * config_.xpad, "", 0);

        // 递归渲染子节点
        render_children_icicle(root, config_.xpad, y, 1, width_per_sample);
    }

    void
    render_children_flamegraph(const FlameNode& node, double x, double parent_y, int depth, double width_per_sample) {
        double child_x = x;
        double child_y = parent_y - config_.frame_height;

        for (const auto& [name, child] : node.children) {
            double child_width = static_cast<double>(child->total_count) * width_per_sample;

            if (child_width >= config_.min_width) {
                render_frame(*child, child_x, child_y, child_width, name, depth);

                if (! child->children.empty()) {
                    render_children_flamegraph(*child, child_x, child_y, depth + 1, width_per_sample);
                }
            }

            child_x += child_width;
        }
    }

    void render_children_icicle(const FlameNode& node, double x, double parent_y, int depth, double width_per_sample) {
        double child_x = x;
        double child_y = parent_y + config_.frame_height;

        for (const auto& [name, child] : node.children) {
            double child_width = static_cast<double>(child->total_count) * width_per_sample;

            if (child_width >= config_.min_width) {
                render_frame(*child, child_x, child_y, child_width, name, depth);

                if (! child->children.empty()) {
                    render_children_icicle(*child, child_x, child_y, depth + 1, width_per_sample);
                }
            }

            child_x += child_width;
        }
    }

    void render_frame(const FlameNode& node, double x, double y, double width, const std::string& name, int depth) {
        // 获取函数名（根节点显示 "all"）
        std::string func_name = name.empty() ? "all" : name;

        // 构建 title（tooltip）
        std::string title = build_frame_title(func_name, node.total_count);

        // 获取颜色
        std::string color = get_frame_color(func_name, depth);

        // 开始 g 元素
        svg_content_ << "<g>\n";
        svg_content_ << "<title>" << escape_xml(title) << "</title>\n";

        // 渲染矩形 - 添加 fg:x 和 fg:w 属性
        svg_content_ << "<rect x=\"" << std::fixed << std::setprecision(1) << x << "\" y=\"" << static_cast<int>(y)
                     << "\" width=\"" << std::setprecision(1) << width << "\" height=\"" << (config_.frame_height - 1)
                     << "\" fill=\"" << color << "\" rx=\"2\" ry=\"2\""
                     << " fg:x=\"" << static_cast<int>(x) << "\" fg:w=\"" << static_cast<int>(width) << "\" />\n";

        // 总是渲染文本元素（即使初始为空）
        std::string display_text = "";
        if (should_render_text(width)) {
            display_text = truncate_text(func_name, width);
        }
        svg_content_ << "<text x=\"" << std::setprecision(2) << (x + 3) << "\" y=\"" << std::setprecision(1)
                     << (y + config_.frame_height - 5) << "\">" << escape_xml(display_text) << "</text>\n";

        svg_content_ << "</g>\n";
    }

    std::string build_frame_title(const std::string& func_name, size_t samples) {
        std::ostringstream title;
        title << func_name;

        if (config_.count_name.empty()) {
            title << " (" << samples << " samples";
        } else {
            title << " (" << samples << " " << config_.count_name;
        }

        if (total_samples_ > 0) {
            double percentage = (static_cast<double>(samples) / static_cast<double>(total_samples_)) * 100.0;
            title << ", " << std::fixed << std::setprecision(2) << percentage << "%)";
        } else {
            title << ")";
        }

        return title.str();
    }

    std::string get_frame_color(const std::string& func_name, int depth) {
        if (func_name == "all" && depth == 0) {
            return "rgb(250,250,250)"; // 根节点用浅色
        }

        if (func_name == "--" || func_name == "-") {
            return "rgb(240,240,240)"; // 分隔符用灰色
        }

        // 计算热度比例：深度越大（越靠近栈顶），热度越高
        double heat_ratio = 0.0;
        if (max_depth_ > 0) {
            heat_ratio = static_cast<double>(depth) / max_depth_;
        }

        return color_scheme_->get_color(func_name, heat_ratio);
    }

    bool should_render_text(double width) const {
        // 估算最少需要显示3个字符加上 ".."
        double min_text_width = 5 * config_.font_size * config_.font_width;
        return width >= min_text_width;
    }

    std::string truncate_text(const std::string& text, double width) {
        // 估算可以显示的字符数
        int available_chars = static_cast<int>((width - 6) / (config_.font_size * config_.font_width));

        if (available_chars <= 0) return "";

        if (static_cast<int>(text.length()) <= available_chars) {
            return text;
        }

        if (available_chars < 3) {
            return "";
        }

        return text.substr(0, available_chars - 2) + "..";
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

    std::string escape_xml(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '"':
                    result += "&quot;";
                    break;
                case '\'':
                    result += "&apos;";
                    break;
                default:
                    result += c;
                    break;
            }
        }
        return result;
    }

    std::string escape_js(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '\\':
                    result += "\\\\";
                    break;
                case '\'':
                    result += "\\'";
                    break;
                case '"':
                    result += "\\\"";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    result += c;
                    break;
            }
        }
        return result;
    }

    void write_to_file(std::string_view output_file) {
        std::ofstream file(output_file.data());
        if (! file.is_open()) {
            throw RenderException(std::string("Cannot create SVG file: ") + output_file.data());
        }

        file << svg_content_.str();

        if (! file.good()) {
            throw RenderException(std::string("Error writing to SVG file: ") + output_file.data());
        }

        file.close();
    }
};

class FlameGraphRendererFactory {
    using CreatorFunc = std::function<std::unique_ptr<FlameGraphRenderer>()>;

    static const std::unordered_map<std::string_view, CreatorFunc>& get_render_map() {
        static const std::unordered_map<std::string_view, CreatorFunc> render_map = {
            { "svg",  []() { return std::make_unique<SvgFlameGraphRenderer>(); }},
            {"html", []() { return std::make_unique<HtmlFlameGraphRenderer>(); }},
        };
        return render_map;
    }

  public:
    static std::unique_ptr<FlameGraphRenderer> create(std::string_view filetype) {
        const auto& map = get_render_map();
        auto it = map.find(filetype);
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
        auto suffix = file_suffix(out_file);
        if (suffix.empty()) {
            throw FlameGraphException("File suffix empty");
        }
        auto renderer = FlameGraphRendererFactory::create(suffix);

        try {
            // 解析原始数据
            auto samples = parser->parse(raw_file);

            if (samples.empty()) {
                throw FlameGraphException("No valid samples found in input file");
            }

            std::cout << "Parsed " << samples.size() << " samples using " << parser->get_using_parser() << "\n";

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
