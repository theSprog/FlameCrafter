#!/usr/bin/env python3
"""
generate_perf_samples.py
生成极简 perf-script 样本，用于基准火焰图工具。
"""

import argparse
import random
import time
from pathlib import Path
from typing import List

# --------------- 可自由增删的函数/库列表 -----------------
FUNCS_APP = [
    "process_request",
    "handle_connection",
    "parse_data",
    "compute_result",
    "execute_query",
    "render_response",
    "validate_input",
    "serialize_output",
    "load_config",
    "init_database",
    "cleanup_resources",
    "log_event",
]
FUNCS_LIBC = ["malloc", "free", "memcpy", "strlen", "strcmp", "printf"]
FUNCS_MISC = ["foo", "bar", "baz", "qux", "alpha", "beta", "gamma", "delta"]

LIBS = [
    "/usr/lib/libc.so.6",
    "/usr/lib/libm.so.6",
    "/usr/lib/libstdc++.so.6",
    "/usr/lib/libssl.so.3",
]


# --------------- 栈帧生成 -----------------
def random_addr() -> str:
    """返回 12 位十六进制地址（假装 48-bit）"""
    return f"{random.randint(0x400000000000, 0x7FFFFFFFFFFF):x}"


def random_off() -> str:
    """返回 1-2 字节十六进制偏移"""
    return f"{random.randint(0x10, 0x2FF):x}"


def build_stack(max_depth: int = 10) -> List[str]:
    depth = random.randint(3, max_depth)
    frames: List[str] = []

    # 热门路径 30% 概率
    if random.random() < 0.3:
        frames = ["main", "process_request", "execute_query"]
    else:
        pool = FUNCS_APP + FUNCS_LIBC + FUNCS_MISC
        frames = random.choices(pool, k=depth)

    return frames


# --------------- 主函数 -----------------
def generate_perf(output: Path, samples: int, pid: int = 12345, comm: str = "testprog"):
    start_ts = time.time()  # 秒
    delta_clock = 250_000  # perf 例子里的 cpu-clock 周期

    with output.open("w") as f:
        for i in range(samples):
            ts = start_ts + i * 0.0001  # 每 0.1 ms 一个样本
            ts_str = f"{ts:.6f}"
            f.write(f"{comm:<16} {pid:>5} {ts_str}:     {delta_clock:>6} cpu-clock:u:\n")

            for func in build_stack():
                addr = random_addr()
                off = random_off()
                lib = random.choice(LIBS) if random.random() < 0.6 else "[unknown]"
                f.write(f"\t{addr} {func}+0x{off} ({lib})\n")

            # 收尾的未知帧
            f.write("\t0 [unknown] ([unknown])\n\n")

    print(f"✅ Generated {samples} samples → {output} ({output.stat().st_size / 1024:.1f} KB)")


# --------------- CLI -----------------
def main():
    ap = argparse.ArgumentParser(description="Generate perf-script style test data")
    ap.add_argument("-o", "--output", default="test_data.perf")
    ap.add_argument("-n", "--samples", type=int, default=1000)
    args = ap.parse_args()

    generate_perf(Path(args.output), args.samples)


if __name__ == "__main__":
    main()
