#pragma once

#include <thread>
#include <future>
#include <atomic>
#include <tbb/concurrent_hash_map.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include "flamegraph.hpp"

// 在原有的头文件基础上添加并行化支持

namespace flamegraph {

// 并行化的行扫描器
class ParallelLineScanner {
    std::string_view buffer;
    std::vector<size_t> line_offsets;
    
public:
    explicit ParallelLineScanner(std::string_view data) : buffer(data) {
        // 预先扫描所有行的偏移量
        line_offsets.push_back(0);
        for (size_t i = 0; i < buffer.size(); ++i) {
            if (buffer[i] == '\n') {
                line_offsets.push_back(i + 1);
            }
        }
        if (line_offsets.back() != buffer.size()) {
            line_offsets.push_back(buffer.size());
        }
    }
    
    size_t line_count() const {
        return line_offsets.empty() ? 0 : line_offsets.size() - 1;
    }
    
    std::string_view get_line(size_t index) const {
        if (index >= line_count()) return {};
        size_t start = line_offsets[index];
        size_t end = (index + 1 < line_offsets.size()) ? line_offsets[index + 1] - 1 : buffer.size();
        return trim(buffer.substr(start, end - start));
    }
    
    // 获取一个块的行范围
    std::pair<size_t, size_t> get_block_range(size_t block_idx, size_t num_blocks) const {
        size_t total_lines = line_count();
        size_t lines_per_block = total_lines / num_blocks;
        size_t start = block_idx * lines_per_block;
        size_t end = (block_idx == num_blocks - 1) ? total_lines : (block_idx + 1) * lines_per_block;
        return {start, end};
    }
};

// 并行化的 PerfScript 解析器
class ParallelPerfScriptParser : public AbstractStackParser {
private:
    static constexpr size_t MIN_LINES_PER_THREAD = 10000;
    
public:
    std::vector<StackSample> parse(std::string_view buffer) override {
        ParallelLineScanner scanner(buffer);
        size_t total_lines = scanner.line_count();
        
        // 确定线程数
        size_t num_threads = std::min(
            static_cast<size_t>(std::thread::hardware_concurrency()),
            (total_lines + MIN_LINES_PER_THREAD - 1) / MIN_LINES_PER_THREAD
        );
        
        if (num_threads <= 1) {
            // 数据量太小，使用单线程
            return parse_single_thread(scanner);
        }
        
        // 并行解析
        std::vector<std::future<std::vector<StackSample>>> futures;
        
        for (size_t i = 0; i < num_threads; ++i) {
            futures.push_back(std::async(std::launch::async, [this, &scanner, i, num_threads]() {
                return parse_block(scanner, i, num_threads);
            }));
        }
        
        // 收集结果
        std::vector<StackSample> all_samples;
        for (auto& future : futures) {
            auto samples = future.get();
            all_samples.insert(all_samples.end(), 
                             std::make_move_iterator(samples.begin()),
                             std::make_move_iterator(samples.end()));
        }
        
        if (all_samples.empty()) {
            throw ParseException("No valid samples found in file");
        }
        
        return all_samples;
    }
    
    std::string_view get_parser_name() const override {
        return "ParallelPerfScriptParser";
    }
    
private:
    std::vector<StackSample> parse_single_thread(const ParallelLineScanner& scanner) {
        std::vector<StackSample> samples;
        StackSample current_sample;
        bool reading_stack = false;
        
        for (size_t i = 0; i < scanner.line_count(); ++i) {
            std::string_view line = scanner.get_line(i);
            
            if (line.empty()) {
                if (reading_stack) {
                    PerfScriptParser::push_valid_sample(samples, current_sample);
                }
                reading_stack = false;
            } else {
                PerfScriptParser::parse_line(line, current_sample, reading_stack);
            }
        }
        
        if (reading_stack) {
            PerfScriptParser::push_valid_sample(samples, current_sample);
        }
        
        return samples;
    }
    
    std::vector<StackSample> parse_block(const ParallelLineScanner& scanner, 
                                        size_t block_idx, 
                                        size_t num_blocks) {
        auto [start, end] = scanner.get_block_range(block_idx, num_blocks);
        std::vector<StackSample> samples;
        StackSample current_sample;
        bool reading_stack = false;
        
        // 如果不是第一个块，需要找到第一个完整的样本开始位置
        if (block_idx > 0) {
            // 向前扫描找到第一个空行或样本头
            while (start < end) {
                std::string_view line = scanner.get_line(start);
                if (line.empty() || line.find(':') != std::string_view::npos) {
                    break;
                }
                start++;
            }
        }
        
        for (size_t i = start; i < end; ++i) {
            std::string_view line = scanner.get_line(i);
            
            if (line.empty()) {
                if (reading_stack) {
                    PerfScriptParser::push_valid_sample(samples, current_sample);
                }
                reading_stack = false;
            } else {
                PerfScriptParser::parse_line(line, current_sample, reading_stack);
            }
        }
        
        // 如果是最后一个块且正在读取堆栈，保存它
        if (block_idx == num_blocks - 1 && reading_stack) {
            PerfScriptParser::push_valid_sample(samples, current_sample);
        }
        
        return samples;
    }
};

// TBB 需要的哈希比较器格式
struct TBBVectorFrameHashCompare {
    static size_t hash(const std::vector<Frame>& frames) {
        size_t h = 0;
        for (const auto& f : frames) {
            size_t combined = Frame::Hasher{}(f);
            h ^= combined + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
    
    static bool equal(const std::vector<Frame>& a, const std::vector<Frame>& b) {
        return a == b;
    }
};

// 并行化的堆栈折叠器
class ParallelStackCollapser {
public:
    CollapsedStack collapse(const std::vector<StackSample>& samples, 
                          const StackCollapseOptions& options = {}) {
        (void)options;
        
        // 使用 TBB 的并发哈希表
        using ConcurrentMap = tbb::concurrent_hash_map<std::vector<Frame>, 
                                                      std::atomic<size_t>, 
                                                      TBBVectorFrameHashCompare>;
        ConcurrentMap concurrent_collapsed;
        
        // 并行处理样本
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, samples.size()),
            [&samples, &concurrent_collapsed](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i) {
                    const auto& sample = samples[i];
                    ConcurrentMap::accessor acc;
                    if (concurrent_collapsed.insert(acc, sample.frames)) {
                        acc->second.store(sample.count);
                    } else {
                        acc->second.fetch_add(sample.count);
                    }
                }
            }
        );
        
        // 转换为普通的 CollapsedStack
        CollapsedStack result;
        for (const auto& pair : concurrent_collapsed) {
            result.collapsed[pair.first] = pair.second.load();
        }
        
        return result;
    }
    
    void write_folded_file(const CollapsedStack& collapsed_stacks,
                          std::string_view filename,
                          const StackCollapseOptions& options = {}) {
        (void)options;
        
        // 先收集所有数据到内存
        std::vector<std::string> lines;
        lines.reserve(collapsed_stacks.collapsed.size());
        
        for (const auto& [frames, count] : collapsed_stacks.collapsed) {
            std::ostringstream oss;
            for (size_t i = 0; i < frames.size(); ++i) {
                if (i > 0) {
                    oss << ';';
                }
                oss << frames[i];
            }
            oss << ' ' << count << '\n';
            lines.push_back(oss.str());
        }
        
        // 一次性写入文件
        std::ofstream ofs(filename.data(), std::ios::binary);
        if (!ofs.is_open()) {
            throw OpenFileException(filename);
        }
        
        for (const auto& line : lines) {
            ofs.write(line.data(), line.size());
        }
    }
};

// 更新后的 AutoDetectParser，使用并行解析器
class ParallelAutoDetectParser : public AbstractStackParser {
private:
    std::unique_ptr<AbstractStackParser> actual_parser_;
    static constexpr int MAX_PREVIEW_LINE = 128;
    
public:
    std::vector<StackSample> parse(std::string_view buffer) override {
        detect_format(buffer);
        if (!actual_parser_) {
            throw ParseException(std::string("Unable to detect file format"));
        }
        
        return actual_parser_->parse(buffer);
    }
    
    std::string_view get_parser_name() const override {
        return "ParallelAutoDetectParser";
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
            
            if (!line.empty()) {
                if (is_like_perf(line)) {
                    has_perf_format = true;
                    break;
                }
            }
            
            lines_checked++;
            start = end + 1;
        }
        
        if (has_perf_format) {
            actual_parser_ = std::make_unique<ParallelPerfScriptParser>();
        } else {
            actual_parser_ = std::make_unique<GenericTextParser>();
        }
    }
    
    bool is_like_perf(std::string_view line) {
        return line.find("cycles:") != std::string_view::npos || 
               line.find("instructions:") != std::string_view::npos ||
               (line.find_first_of("0123456789abcdef") == 0 && 
                line.find("(") != std::string_view::npos);
    }
};

// 更新的火焰图生成器，使用并行组件
class ParallelFlameGraphGenerator {
private:
    FlameGraphConfig config_;
    StackCollapseOptions collapse_opts_;
    FlameGraphBuildOptions build_opts_;
    
public:
    explicit ParallelFlameGraphGenerator(const FlameGraphConfig& config = {}) 
        : config_(config) {
        config_.validate();
    }
    
    void generate(std::string_view in_file, std::string_view out_file) {
        auto parser = std::make_unique<ParallelAutoDetectParser>();
        ParallelStackCollapser collapser;
        FlameGraphBuilder builder;
        auto suffix = file_suffix(out_file);
        if (suffix.empty()) {
            throw FlameGraphException(std::string("File suffix empty") + out_file.data());
        }
        auto renderer = FlameGraphRendererFactory::create(suffix);
        
        try {
            MMapBuffer buffer(in_file);
            
            // 并行解析原始数据
            std::vector<StackSample> samples = parser->parse(buffer.view());
            
            if (samples.empty()) {
                throw FlameGraphException("No valid samples found in input file");
            }
            
            // 并行折叠堆栈
            CollapsedStack collapsed = collapser.collapse(samples, collapse_opts_);
            
            if (collapsed.empty()) {
                throw FlameGraphException("No stacks remained after collapsing");
            }
            
            if (config_.write_folded_file) {
                collapser.write_folded_file(collapsed, std::string(out_file) + ".collapse");
            }
            
            // 构建树（这部分较难并行化，保持原样）
            build_opts_.max_depth = config_.max_depth;
            build_opts_.prune_threshold = config_.min_heat_threshold;
            FlameNode* root = builder.build_tree(collapsed, build_opts_);
            
            if (root->total_count == 0) {
                throw FlameGraphException("Tree has no samples");
            }
            
            renderer->render(*root, out_file);

            root->destroy_tree();
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