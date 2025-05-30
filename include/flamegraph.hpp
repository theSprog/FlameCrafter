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
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <stdexcept>
#include <regex>
#include <cassert>

#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

namespace flamegraph {

class FlameGraphException : public std::runtime_error {
  public:
    explicit FlameGraphException(std::string_view message)
        : std::runtime_error(std::string("FlameGraph Error: ") + message.data()) {}
};

class MemoryException : public std::runtime_error {
  public:
    explicit MemoryException(std::string_view message)
        : std::runtime_error(std::string("Memory Error: ") + message.data()) {}
};

class FileNotFoundException : public std::runtime_error {
  public:
    explicit FileNotFoundException(std::string_view message)
        : std::runtime_error(std::string("File not found: ") + message.data()) {}
};

class OpenFileException : public std::runtime_error {
  public:
    explicit OpenFileException(std::string_view message)
        : std::runtime_error(std::string("Cannot open file: ") + message.data()) {}
};

class ParseException : public FlameGraphException {
  public:
    explicit ParseException(std::string_view message)
        : FlameGraphException(std::string("Parse Error: ") + message.data()) {}
};

class RenderException : public FlameGraphException {
  public:
    explicit RenderException(std::string_view message)
        : FlameGraphException(std::string("Render Error: ") + message.data()) {}
};

namespace {
template <typename T>
std::string to_string(const T& obj) {
    std::ostringstream oss;
    oss << obj;
    return oss.str();
}

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

struct MMapBuffer {
    void* addr;
    size_t size;

    MMapBuffer(std::string_view filename) {
        int fd = open(filename.data(), O_RDONLY);
        if (fd == -1) throw MemoryException("open failed");
        size = lseek(fd, 0, SEEK_END);
        addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) throw MemoryException("mmap failed");
        close(fd);
    }

    ~MMapBuffer() {
        munmap(addr, size);
    }

    std::string_view view() const {
        return {static_cast<char*>(addr), size};
    }
};

struct LineScanner {
    std::string_view buffer;
    size_t pos = 0;
    size_t line_number = 0;

    explicit LineScanner(std::string_view data) : buffer(data) {}

    // 获取下一行，trim 后返回；如果读完，返回空 view
    std::string_view next_trimmed_line() {
        if (pos >= buffer.size()) {
            return {};
        }

        size_t end = buffer.find('\n', pos);
        if (end == std::string_view::npos) end = buffer.size();

        std::string_view line = buffer.substr(pos, end - pos);
        pos = end + 1;
        line_number++;

        return trim(line);
    }

    // 是否读完
    bool eof() const {
        return pos >= buffer.size();
    }
};

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

// 统计信息结构
struct TreeStats {
    size_t total_nodes = 0;
    size_t leaf_nodes = 0;
    int max_depth = 0;
    size_t total_samples = 0;
    std::vector<size_t> depth_distribution;
};

struct Frame {
    struct Hasher {
        size_t operator()(const Frame& f) const noexcept {
            size_t h1 = std::hash<std::string_view>{}(f.name);
            size_t h2 = std::hash<bool>{}(f.is_func);
            size_t h3 = std::hash<bool>{}(f.lib_include_brackets);
            size_t combined = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            combined ^= h3 + 0x9e3779b9 + (combined << 6) + (combined >> 2);
            return combined;
        }
    };

    std::string_view name; // 底层零拷贝视图
    bool is_func;
    bool lib_include_brackets; // 是否已经加了 [xxx]

    // 可扩展字段: pid, 线程id, 采样率等

    Frame() : Frame("") {}

    explicit Frame(std::string_view name, bool is_func = true, bool lib_include_brackets = false)
        : name(name), is_func(is_func), lib_include_brackets(lib_include_brackets) {}

    bool operator==(const Frame& other) const noexcept {
        return name == other.name && is_func == other.is_func && lib_include_brackets == other.lib_include_brackets;
    }

    bool empty() {
        return name.empty();
    }

    friend inline std::ostream& operator<<(std::ostream& os, const Frame& f) {
        if (! f.is_func && ! f.lib_include_brackets) {
            os << "[";
        }
        os << (f.name.empty() ? "root" : f.name);
        if (! f.is_func && ! f.lib_include_brackets) {
            os << "]";
        }
        return os;
    }
};

struct FlameNode {
    Frame frame;
    size_t self_count = 0;
    size_t total_count = 0;
    std::unordered_map<Frame, std::unique_ptr<FlameNode>, Frame::Hasher> children;
    FlameNode* parent = nullptr; // 父节点指针

    FlameNode() = default;

    explicit FlameNode(Frame frame) : frame(frame) {}

    // 禁用拷贝/移动构造函数
    FlameNode(FlameNode&& other) noexcept = delete;
    FlameNode& operator=(FlameNode&& other) noexcept = delete;
    FlameNode(const FlameNode&) = delete;
    FlameNode& operator=(const FlameNode&) = delete;

    FlameNode* get_or_create_child(Frame child_frame) {
        auto it = children.find(child_frame);
        if (it == children.end()) {
            auto child = std::make_unique<FlameNode>(child_frame);
            FlameNode* child_ptr = child.get();
            child_ptr->parent = this; // 设置父指针
            children[child_frame] = std::move(child);
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
        oss << "\"name\":\"" << frame << "\",";
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

struct StackSample {
    std::vector<Frame> frames;
    size_t count = 1;
    std::string_view process_name;
    uint64_t timestamp = 0;

    StackSample() = default;

    StackSample(std::vector<Frame> stack_frames, size_t sample_count = 1)
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

    virtual std::vector<StackSample> parse(std::string_view buffer) = 0;
    virtual std::string_view get_parser_name() const = 0;
};

/**
 * @brief 适配 perf script 收集的堆栈
 */
class PerfScriptParser : public AbstractStackParser {
  public:
    std::vector<StackSample> parse(std::string_view buffer) override {
        std::vector<StackSample> samples;
        StackSample current_sample;
        bool reading_stack = false;
        LineScanner scanner(buffer);

        while (true) {
            std::string_view line = scanner.next_trimmed_line();
            if (line.empty() && scanner.eof()) break;

            if (line.empty()) { // 空行：当前 stack 结束
                if (reading_stack) {
                    push_valid_sample(samples, current_sample);
                }
                reading_stack = false;
            } else { // 非空行：做解析
                parse_line(line, current_sample, reading_stack);
            }
        }

        // 文件结束后，最后一个样本（如果有）
        if (reading_stack) {
            push_valid_sample(samples, current_sample);
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

    Frame parse_perf_stack_frame(std::string_view line) {
        // i.e. "7f0b8bf5766d malloc+0x5d (/usr/lib/libc.so.6)"
        size_t first_space = line.find(' '); // 跳过 address
        if (first_space == std::string::npos) return Frame{};

        std::string_view content = line.substr(first_space + 1);
        std::string_view func_name{};
        std::string_view lib_name{};
        bool lib_include_brackets = false;

        size_t paren_start = content.rfind('(');
        size_t paren_end = content.find(')', paren_start);

        if (paren_start != std::string::npos && paren_end != std::string::npos) {
            lib_name = content.substr(paren_start + 1, paren_end - paren_start - 1);
            func_name = trim(content.substr(0, paren_start));
        } else {
            func_name = content;
        }

        if (func_name != "[unknown]") {
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
            if (lib_name.front() == '[' && lib_name.back() == ']') {
                lib_include_brackets = true;
            }
        }

        if (! func_name.empty() && func_name != "[unknown]") {
            return Frame(func_name);
        } else {
            // 如果没有 func_name 则使用 lib 替代
            return Frame{lib_name, false, lib_include_brackets};
        }
    }

    // 只有合法的 sample 才会被 push
    void push_valid_sample(std::vector<StackSample>& samples, StackSample& current_sample) {
        if (! current_sample.frames.empty()) {
            std::reverse(current_sample.frames.begin(), current_sample.frames.end());
            if (current_sample.is_valid()) {
                samples.push_back(std::move(current_sample));
            }
            current_sample = StackSample();
        }
    }

    void parse_line(std::string_view line_view, StackSample& current_sample, bool& reading_stack) {
        if (! reading_stack && line_view.find(':') != std::string::npos) {
            parse_sample_header(line_view, current_sample);
            reading_stack = true;
        } else if (reading_stack) {
            Frame frame = parse_perf_stack_frame(line_view);
            if (! frame.empty()) {
                current_sample.frames.push_back(frame);
            }
        }
    }
};

/**
 * @brief 适配最常见的“手动采样堆栈”格式
 */
class GenericTextParser : public AbstractStackParser {
  public:
    std::vector<StackSample> parse(std::string_view buffer) override {
        std::vector<StackSample> samples;
        std::vector<Frame> current_stacks;
        LineScanner scanner(buffer);

        while (true) {
            std::string_view line = scanner.next_trimmed_line();
            if (line.empty() && scanner.eof()) break;

            // 跳过空行和注释
            if (line.empty() || line[0] == '#') {
                if (! current_stacks.empty()) {
                    samples.emplace_back(std::move(current_stacks));
                    current_stacks.clear();
                }
                continue;
            }

            // 非空、非注释，拷贝到 stack（因为外部仍然需要所有权）
            current_stacks.emplace_back(line);
        }

        // 文件结束后，最后一个 stack（如果有）
        if (! current_stacks.empty()) {
            samples.emplace_back(std::move(current_stacks));
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
    static constexpr int MAX_PREVIEW_LINE = 128;

  public:
    std::vector<StackSample> parse(std::string_view buffer) override {
        detect_format(buffer);
        if (! actual_parser_) {
            throw ParseException(std::string("Unable to detect file format for: ") + buffer.data());
        }

        return actual_parser_->parse(buffer);
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
    void detect_format(std::string_view buffer) {
        size_t start = 0;
        int lines_checked = 0;
        bool has_perf_format = false;

        while (start < buffer.size() && lines_checked < MAX_PREVIEW_LINE) {
            size_t end = buffer.find('\n', start);
            if (end == std::string_view::npos) end = buffer.size();

            std::string_view line = buffer.substr(start, end - start);
            line = trim(line);

            if (! line.empty()) {
                if (is_like_perf(line)) {
                    has_perf_format = true;
                    break;
                }
            }

            lines_checked++;
            start = end + 1; // 下一行
        }

        if (has_perf_format) {
            actual_parser_ = std::make_unique<PerfScriptParser>();
        } else {
            actual_parser_ = std::make_unique<GenericTextParser>();
        }
    }

    bool is_like_perf(std::string_view line) {
        return line.find("cycles:") != std::string_view::npos || line.find("instructions:") != std::string_view::npos ||
               (line.find_first_of("0123456789abcdef") == 0 && line.find("(") != std::string_view::npos);
    }
};

// 🔥 ===== 堆栈折叠器 =====
struct StackCollapseOptions {
    bool merge_kernel_user = false;           // 合并内核和用户空间
    bool ignore_libraries = false;            // 忽略库名
    std::vector<std::string> filter_patterns; // 过滤模式
    size_t min_count_threshold = 1;           // 最小计数阈值
};

struct CollapsedStack {
    struct VectorFrameHash {
        size_t operator()(const std::vector<Frame>& frames) const noexcept {
            size_t hash = 0;
            for (const auto& f : frames) {
                size_t combined = Frame::Hasher{}(f);
                hash ^= combined + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    std::unordered_map<std::vector<Frame>, size_t, VectorFrameHash> collapsed;

    bool empty() const {
        return collapsed.empty();
    }
};

class StackCollapser {
  public:
    // 折叠堆栈: 读入样本，生成 folded 文件数据
    CollapsedStack collapse(const std::vector<StackSample>& samples, const StackCollapseOptions& options = {}) {
        (void)options;
        CollapsedStack collapsed_stacks;

        for (const auto& sample : samples) {
            // 统计出现次数
            collapsed_stacks.collapsed[sample.frames] += sample.count;
        }

        return collapsed_stacks;
    }

    // 写 folded 文件
    void write_folded_file(const CollapsedStack& collapsed_stacks,
                           std::string_view filename,
                           const StackCollapseOptions& options = {}) {
        (void)options;
        std::ofstream ofs(filename.data());
        if (! ofs.is_open()) {
            throw OpenFileException(filename);
        }

        for (const auto& [frames, count] : collapsed_stacks.collapsed) {
            for (size_t i = 0; i < frames.size(); ++i) {
                if (i > 0) {
                    ofs << ';';
                }
                ofs << frames[i];
            }
            ofs << ' ' << count << '\n';
        }
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
    std::unique_ptr<FlameNode> build_tree(const CollapsedStack& folded_stacks,
                                          const FlameGraphBuildOptions& options = {}) {

        auto root = std::make_unique<FlameNode>(Frame("root"));

        for (const auto& [stack_frame, count] : folded_stacks.collapsed) {
            if (stack_frame.empty()) continue;

            FlameNode* current = root.get();
            for (const auto& frame : stack_frame) {
                current = current->get_or_create_child(frame);
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

    std::ofstream svg_content_;
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

        svg_content_.open(output_file.data());
        if (! svg_content_.is_open()) {
            throw RenderException(std::string("Cannot create SVG file: ") + output_file.data());
        }

        // 写入 svg
        write_svg(root);

        if (! svg_content_.good()) {
            throw RenderException(std::string("Error writing to SVG file: ") + output_file.data());
        }

        svg_content_.close();
    }

  private:
    void write_svg(const FlameNode& root) {
        // 清空内容
        svg_content_.clear();

        // 计算图像高度
        max_depth_ = calculate_tree_height(root);
        int imageheight = calculate_image_height();

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
    }

    void setup_color_scheme() {
        color_scheme_ = ColorSchemeFactory::create(config_.colors);
    }

    size_t estimate_reserve_size(size_t sample_count) {
        size_t bytes_per_node = 514;   // 平均每个 Frame: 需要 400-600 字节
        size_t fixed_overhead = 15000; // 固定开销（主要是JS）
        return fixed_overhead + (sample_count * bytes_per_node);
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
        svg_content_ << "var nametype = '" << config_.name_type << "';\n\n";

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

        render_frame(root, config_.xpad, y, config_.width - 2 * config_.xpad, Frame(""), 0);

        // 递归渲染子节点
        render_children_flamegraph(root, config_.xpad, y, 1, width_per_sample);
    }

    void render_frames_icicle(const FlameNode& root) {
        int ypad1 = config_.font_size * 3;
        int ypad3 = config_.subtitle.empty() ? 0 : config_.font_size * 2;

        double width_per_sample = (config_.width - 2.0 * config_.xpad) / static_cast<double>(total_samples_);
        double y = ypad1 + ypad3;

        // 渲染根节点
        render_frame(root, config_.xpad, y, config_.width - 2 * config_.xpad, Frame(""), 0);

        // 递归渲染子节点
        render_children_icicle(root, config_.xpad, y, 1, width_per_sample);
    }

    void
    render_children_flamegraph(const FlameNode& node, double x, double parent_y, int depth, double width_per_sample) {
        double child_x = x;
        double child_y = parent_y - config_.frame_height;

        for (const auto& [frame, child] : node.children) {
            double child_width = static_cast<double>(child->total_count) * width_per_sample;

            if (child_width >= config_.min_width) {
                render_frame(*child, child_x, child_y, child_width, frame, depth);

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

    void render_frame(const FlameNode& node, double x, double y, double width, Frame frame, int depth) {
        // 构建 title（tooltip）
        std::string title = build_frame_title(frame, node.total_count);

        // 获取颜色
        std::string color = get_frame_color(frame.name, depth);

        // 开始 g 元素
        svg_content_ << "<g>\n";

        // 写 title
        svg_content_ << "<title>";
        escape_xml_to_stream(title, svg_content_);
        svg_content_ << "</title>\n";

        svg_content_ << "<rect x=\"" << std::fixed << std::setprecision(1) << x << "\" y=\"" << static_cast<int>(y)
                     << "\" width=\"" << std::setprecision(1) << width << "\" height=\"" << (config_.frame_height - 1)
                     << "\" fill=\"" << color << "\" rx=\"2\" ry=\"2\" />\n";

        // 留空，直接让浏览器去 title 里面拿信息
        svg_content_ << "<text x=\"" << std::setprecision(2) << (x + 3) << "\" y=\"" << std::setprecision(1)
                     << (y + config_.frame_height - 5) << "\"></text>\n";

        svg_content_ << "</g>\n";
    }

    void escape_xml_to_stream(std::string_view str, std::ofstream& os) {
        for (char c : str) {
            switch (c) {
                case '&':
                    os << "&amp;";
                    break;
                case '<':
                    os << "&lt;";
                    break;
                case '>':
                    os << "&gt;";
                    break;
                case '"':
                    os << "&quot;";
                    break;
                case '\'':
                    os << "&apos;";
                    break;
                default:
                    os << c;
                    break;
            }
        }
    }

    std::string build_frame_title(Frame frame, size_t samples) {
        std::ostringstream title;
        title << frame;

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

    std::string get_frame_color(std::string_view func_name, int depth) {
        if (func_name == "root" && depth == 0) {
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
    StackCollapseOptions collapse_opts_;
    FlameGraphBuildOptions build_opts_;

  public:
    explicit FlameGraphGenerator(const FlameGraphConfig& config = {}) : config_(config) {
        config_.validate();
    }

    void generate_from(std::string_view raw_file, std::string_view out_file) {
        auto parser = std::make_unique<AutoDetectParser>();
        StackCollapser collapser;
        FlameGraphBuilder builder;
        auto suffix = file_suffix(out_file);
        if (suffix.empty()) {
            throw FlameGraphException(std::string("File suffix empty") + out_file.data());
        }
        auto renderer = FlameGraphRendererFactory::create(suffix);

        try {
            MMapBuffer buffer(raw_file);

            // 解析原始数据
            std::vector<StackSample> samples = parser->parse(buffer.view());

            if (samples.empty()) {
                throw FlameGraphException("No valid samples found in input file");
            }

            // 折叠堆栈
            CollapsedStack collapsed = collapser.collapse(samples, collapse_opts_);

            if (collapsed.empty()) {
                throw FlameGraphException("No stacks remained after collapsing");
            }

            if (config_.write_folded_file) {
                collapser.write_folded_file(collapsed, std::string(out_file) + ".collapse");
            }

            // 构建树
            build_opts_.max_depth = config_.max_depth;
            build_opts_.prune_threshold = config_.min_heat_threshold;
            auto root = builder.build_tree(collapsed, build_opts_);

            if (root->total_count == 0) {
                throw FlameGraphException("Tree has no samples");
            }

            renderer->render(*root, out_file);
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
