#!/usr/bin/env python3
"""
火焰图生成器性能基准测试脚本
用于对比不同规模数据的处理性能
"""

import subprocess
import time
import os
import sys
from typing import Dict, List, Tuple

class FlameGraphBenchmark:
    def __init__(self):
        BASE_DIR = os.path.dirname(os.path.abspath(__file__))
        self.test_configs = [
            ("small", 1000, "1K"),
            ("medium", 10000, "10K"), 
            ("large", 100000, "100K"),
        ]
        print("BASE_DIR: ", BASE_DIR)
        self.executable = os.path.join(BASE_DIR, "../flamegraph_main")
        self.generate_data_script = os.path.join(BASE_DIR, "../script/generate_test_data.py")
        
    def ensure_executable_exists(self) -> bool:
        """确保可执行文件存在"""
        if not os.path.exists(self.executable):
            print("❌ Executable not found. Building...")
            result = subprocess.run(["make", "all"], capture_output=True, text=True)
            if result.returncode != 0:
                print(f"❌ Build failed: {result.stderr}")
                return False
            print("✅ Build successful")
        return True
    
    def generate_test_data(self, size_name: str, sample_count: int) -> str:
        """生成测试数据"""
        test_file = f"{size_name}_test.txt"
        
        print(f"📊 Generating {size_name} test data ({sample_count:,} samples)...")
        
        cmd = [
            "python3", self.generate_data_script, 
            "--output", test_file,
            "--samples", str(sample_count)
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"Failed to generate test data: {result.stderr}")
        
        return test_file
    
    def run_benchmark(self, input_file: str, output_file: str) -> Dict[str, float]:
        """运行单个基准测试"""
        # 预热运行
        subprocess.run([self.executable, input_file, output_file], 
                      capture_output=True, text=True)
        
        # 正式测试 - 运行3次取平均值
        times = []
        for i in range(3):
            start_time = time.time()
            result = subprocess.run(
                [self.executable, input_file, output_file],
                capture_output=True, text=True
            )
            end_time = time.time()
            
            if result.returncode != 0:
                raise RuntimeError(f"Benchmark failed: {result.stderr}")
            
            times.append(end_time - start_time)
        
        # 计算统计数据
        avg_time = sum(times) / len(times)
        min_time = min(times)
        max_time = max(times)
        
        return {
            "avg": avg_time,
            "min": min_time, 
            "max": max_time,
            "times": times
        }
    
    def get_file_info(self, filename: str) -> Dict[str, any]:
        """获取文件信息"""
        if not os.path.exists(filename):
            return {"size": 0, "lines": 0}
        
        file_size = os.path.getsize(filename)
        
        # 计算行数
        line_count = 0
        with open(filename, 'r') as f:
            for _ in f:
                line_count += 1
        
        return {
            "size": file_size,
            "lines": line_count
        }
    
    def format_time(self, seconds: float) -> str:
        """格式化时间显示"""
        if seconds < 1:
            return f"{seconds*1000:.1f}ms"
        else:
            return f"{seconds:.2f}s"
    
    def format_size(self, bytes_size: int) -> str:
        """格式化文件大小"""
        if bytes_size < 1024:
            return f"{bytes_size}B"
        elif bytes_size < 1024 * 1024:
            return f"{bytes_size/1024:.1f}KB"
        else:
            return f"{bytes_size/1024/1024:.1f}MB"
    
    def run_all_benchmarks(self) -> List[Tuple[str, Dict]]:
        """运行所有基准测试"""
        results = []
        
        print("🔥 Starting Flame Graph Performance Benchmark")
        print("=" * 50)
        
        for size_name, sample_count, display_name in self.test_configs:
            print(f"\n📊 Testing {display_name} dataset...")
            print("-" * 30)
            
            try:
                # 生成测试数据
                input_file = self.generate_test_data(size_name, sample_count)
                output_file = f"{size_name}_benchmark.svg"
                
                # 获取输入文件信息
                file_info = self.get_file_info(input_file)
                print(f"   Input file: {self.format_size(file_info['size'])}, {file_info['lines']:,} lines")
                
                # 运行基准测试
                print("   Running benchmark...")
                benchmark_result = self.run_benchmark(input_file, output_file)
                
                # 获取输出文件信息
                output_info = self.get_file_info(output_file)
                
                # 记录结果
                result = {
                    "size_name": size_name,
                    "display_name": display_name,
                    "sample_count": sample_count,
                    "input_size": file_info["size"],
                    "input_lines": file_info["lines"],
                    "output_size": output_info["size"],
                    "benchmark": benchmark_result
                }
                results.append((display_name, result))
                
                # 显示结果
                print(f"   ✅ Average time: {self.format_time(benchmark_result['avg'])}")
                print(f"   📈 Range: {self.format_time(benchmark_result['min'])} - {self.format_time(benchmark_result['max'])}")
                print(f"   📄 Output SVG: {self.format_size(output_info['size'])}")
                
                # 计算处理速度
                samples_per_sec = sample_count / benchmark_result['avg']
                print(f"   ⚡ Processing rate: {samples_per_sec:,.0f} samples/sec")
                
            except Exception as e:
                print(f"   ❌ Failed: {e}")
                continue
        
        return results
    
    def print_summary(self, results: List[Tuple[str, Dict]]):
        """打印测试总结"""
        if not results:
            print("\n❌ No benchmark results to summarize")
            return
        
        print("\n" + "=" * 60)
        print("🏆 BENCHMARK SUMMARY")
        print("=" * 60)
        
        print(f"{'Dataset':<10} {'Samples':<10} {'Input':<10} {'Time':<12} {'Rate':<15} {'Output':<10}")
        print("-" * 70)
        
        for display_name, result in results:
            avg_time = result["benchmark"]["avg"]
            rate = result["sample_count"] / avg_time
            
            print(f"{display_name:<10} "
                  f"{result['sample_count']:>9,} "
                  f"{self.format_size(result['input_size']):<10} "
                  f"{self.format_time(avg_time):<12} "
                  f"{rate:>10,.0f} smp/s "
                  f"{self.format_size(result['output_size']):<10}")
        
        # 性能分析
        if len(results) >= 2:
            print(f"\n📈 Performance Analysis:")
            small_rate = results[0][1]["sample_count"] / results[0][1]["benchmark"]["avg"]
            for i in range(1, len(results)):
                current_rate = results[i][1]["sample_count"] / results[i][1]["benchmark"]["avg"]
                ratio = current_rate / small_rate
                print(f"   {results[i][0]} is {ratio:.1f}x as efficient as {results[0][0]} (per sample)")

def main():
    benchmark = FlameGraphBenchmark()
    
    # 检查环境
    if not benchmark.ensure_executable_exists():
        return 1
    
    try:
        # 运行基准测试
        results = benchmark.run_all_benchmarks()
        
        # 打印总结
        benchmark.print_summary(results)
        
        print(f"\n🎉 Benchmark completed! Generated {len(results)} test results.")
        
    except KeyboardInterrupt:
        print("\n\n⚠️  Benchmark interrupted by user")
        return 1
    except Exception as e:
        print(f"\n❌ Benchmark failed: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())