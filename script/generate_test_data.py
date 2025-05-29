#!/usr/bin/env python3
"""
生成火焰图测试数据的脚本
支持生成不同规模和复杂度的测试数据
"""

import random
import argparse
import sys
from typing import List

class FlameTestDataGenerator:
    def __init__(self):
        # 模拟真实的函数调用栈
        self.system_funcs = [
            'main', '__libc_start_main', '_start', 'exit'
        ]
        
        self.application_funcs = [
            'process_request', 'handle_connection', 'parse_data', 'compute_result',
            'execute_query', 'render_response', 'validate_input', 'serialize_output',
            'load_config', 'init_database', 'cleanup_resources', 'log_event'
        ]
        
        self.library_funcs = [
            'malloc', 'free', 'memcpy', 'strlen', 'strcmp', 'printf', 'fprintf',
            'fopen', 'fclose', 'read', 'write', 'socket', 'bind', 'listen', 'accept'
        ]
        
        self.deep_funcs = [
            'foo', 'bar', 'baz', 'qux', 'alpha', 'beta', 'gamma', 'delta',
            'func_a', 'func_b', 'func_c', 'helper_1', 'helper_2', 'util_func'
        ]
    
    def generate_realistic_stack(self, max_depth: int = 10) -> List[str]:
        """生成更真实的调用栈"""
        depth = random.randint(3, max_depth)
        stack = []
        
        # 总是从系统函数开始
        if random.random() > 0.3:  # 70% 概率包含系统函数
            stack.append(random.choice(self.system_funcs))
        
        # 添加应用层函数
        remaining_depth = depth - len(stack)
        if remaining_depth > 0:
            # 选择主要的应用函数类型
            if random.random() > 0.5:
                # 应用逻辑为主
                app_count = min(remaining_depth, random.randint(1, 3))
                stack.extend(random.choices(self.application_funcs, k=app_count))
                remaining_depth -= app_count
            
            # 添加库函数调用
            if remaining_depth > 0 and random.random() > 0.6:
                lib_count = min(remaining_depth, random.randint(1, 2))
                stack.extend(random.choices(self.library_funcs, k=lib_count))
                remaining_depth -= lib_count
            
            # 填充剩余深度
            if remaining_depth > 0:
                stack.extend(random.choices(self.deep_funcs, k=remaining_depth))
        
        return stack
    
    def generate_simple_stack(self, max_depth: int = 8) -> List[str]:
        """生成简单的调用栈"""
        depth = random.randint(2, max_depth)
        all_funcs = self.system_funcs + self.application_funcs + self.deep_funcs
        return random.choices(all_funcs, k=depth)
    
    def generate_hot_path_stack(self) -> List[str]:
        """生成热点路径（高频调用栈）"""
        hot_paths = [
            ['main', 'process_request', 'execute_query', 'malloc'],
            ['main', 'handle_connection', 'read', 'memcpy'],
            ['main', 'render_response', 'serialize_output', 'printf'],
            ['_start', '__libc_start_main', 'main', 'compute_result', 'foo', 'bar'],
            ['main', 'parse_data', 'validate_input', 'strcmp']
        ]
        
        base_path = random.choice(hot_paths)
        # 有概率添加额外的深度
        if random.random() > 0.7:
            extra_funcs = random.choices(self.deep_funcs, k=random.randint(1, 3))
            base_path.extend(extra_funcs)
        
        return base_path
    
    def generate_test_data(self, output_file: str, sample_count: int = 1000, 
                          realistic: bool = True, hot_ratio: float = 0.3):
        """
        生成测试数据
        
        Args:
            output_file: 输出文件路径
            sample_count: 样本数量
            realistic: 是否生成真实的调用栈
            hot_ratio: 热点路径的比例
        """
        
        print(f"🔥 Generating {sample_count} samples...")
        
        with open(output_file, 'w') as f:
            for i in range(sample_count):
                if i % 1000 == 0 and i > 0:
                    print(f"   Generated {i} samples...")
                
                # 决定生成哪种类型的调用栈
                rand = random.random()
                if rand < hot_ratio:
                    # 生成热点路径
                    stack = self.generate_hot_path_stack()
                elif realistic:
                    # 生成真实的调用栈
                    stack = self.generate_realistic_stack()
                else:
                    # 生成简单的调用栈
                    stack = self.generate_simple_stack()
                
                # 写入调用栈
                for func in stack:
                    f.write(func + '\n')
                f.write('\n')  # 空行分隔不同的样本
        
        print(f"✅ Generated test data: {output_file}")
        print(f"   Total samples: {sample_count}")
        print(f"   Hot path ratio: {hot_ratio:.1%}")

def main():
    parser = argparse.ArgumentParser(description='Generate flame graph test data')
    parser.add_argument('-o', '--output', default='test_data.txt',
                       help='Output file name (default: test_data.txt)')
    parser.add_argument('-n', '--samples', type=int, default=1000,
                       help='Number of samples to generate (default: 1000)')
    parser.add_argument('--simple', action='store_true',
                       help='Generate simple test data instead of realistic')
    parser.add_argument('--hot-ratio', type=float, default=0.3,
                       help='Ratio of hot path samples (default: 0.3)')
    
    # 预定义的配置
    parser.add_argument('--small', action='store_true',
                       help='Generate small dataset (1K samples)')
    parser.add_argument('--medium', action='store_true', 
                       help='Generate medium dataset (10K samples)')
    parser.add_argument('--large', action='store_true',
                       help='Generate large dataset (100K samples)')
    parser.add_argument('--huge', action='store_true',
                       help='Generate huge dataset (1M samples)')
    
    args = parser.parse_args()
    
    # 处理预定义配置
    if args.small:
        args.samples = 1000
        args.output = 'small_test.txt'
    elif args.medium:
        args.samples = 10000
        args.output = 'medium_test.txt'
    elif args.large:
        args.samples = 100000
        args.output = 'large_test.txt'
    elif args.huge:
        args.samples = 1000000
        args.output = 'huge_test.txt'
    
    generator = FlameTestDataGenerator()
    
    try:
        generator.generate_test_data(
            output_file=args.output,
            sample_count=args.samples,
            realistic=not args.simple,
            hot_ratio=args.hot_ratio
        )
        
        # 显示文件信息
        import os
        file_size = os.path.getsize(args.output)
        print(f"📊 File size: {file_size:,} bytes ({file_size/1024/1024:.1f} MB)")
        
    except Exception as e:
        print(f"❌ Error: {e}", file=sys.stderr)
        return 1
    
    return 0

if __name__ == '__main__':
    exit(main())