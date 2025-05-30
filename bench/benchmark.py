#!/usr/bin/env python3
"""
bench/benchmark.py  ——  综合基准：
1. stackcollapse-perf.pl + flamegraph.pl
2. inferno-collapse-perf + inferno-flamegraph
3. 自己的单核版  (./flamegraph_main)
4. 自己的并行版  (./flamegraph_main_par)

基准流程：
• 生成 perf-script 测试数据
• hyperfine 统一测量四个命令
• 导出 JSON + 终端 Markdown 表格
"""

import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List
from rich.console import Console
from rich.table import Table
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


# ---------- 配置 ---------- #
BASE_DIR = Path(__file__).parent.resolve()
DATA_GEN = BASE_DIR / "../script/generate_perf_samples.py"
PERF_FILE = "test_data.perf"

console = Console()

TOOLS: Dict[str, str] = {
    "perl": "stackcollapse-perf.pl {input} | flamegraph.pl > {output}",
    "inferno": "inferno-collapse-perf {input} > inferno.folded && inferno-flamegraph inferno.folded > {output}",
    "my_single": "./flamegraph_main {input} {output}",
    "my_parallel": "./flamegraph_main_par {input} {output}",
}

DATASETS: List[tuple] = [
    ("cute", 1_0, "10"),
    ("small", 1_00, "100"),
    ("medium", 1_000, "1 K"),
    ("large", 10_000, "10 K"),
    ("huge", 1_000_00, "100 K"),
    # ("gigantic", 10_000_00, "1 M"),
]

HYPERFINE_COMMON = ["--warmup", "3", "--runs", "10", "--ignore-failure", "--show-output"]


def save_bar_chart(rows, outfile: str = "benchmark_chart.svg"):
    """
    rows: [{'tag': '1 K', 'perl': '12.9', 'inferno': '5.3', ...}, ...]
    """
    print(f"📊 Prepare generate chart to {outfile}")
    tools = ["perl", "inferno", "my_single", "my_parallel"]
    colors = ["#4E79A7", "#59A14F", "#F28E2B", "#E15759"]

    n_rows = len(rows)
    fig, axs = plt.subplots(1, n_rows, figsize=(4 * n_rows, 5), sharey=False)

    # 如果只有一个标签，axs 不是列表，需要统一
    if n_rows == 1:
        axs = [axs]

    for idx, row in enumerate(rows):
        ax = axs[idx]
        x_indexes = np.arange(len(tools))
        values = [float(row.get(t, "nan")) for t in tools]

        bars = ax.bar(x_indexes, values, color=colors, width=0.6)

        ax.set_title(f"Benchmark: {row['tag']} samples")
        ax.set_xticks(x_indexes)
        ax.set_xticklabels(tools, rotation=45, ha="right")
        ax.set_ylabel("Mean time (ms)")

        # 可选：根据数值范围灵活调整 y 轴范围
        max_val = max(values)
        ax.set_ylim(0, max_val * 1.2)

        # 在柱子顶部标注数值
        for bar in bars:
            height = bar.get_height()
            ax.annotate(
                f"{height:.1f}",
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3),  # 垂直偏移
                textcoords="offset points",
                ha="center",
                va="bottom",
                fontsize=8,
            )

    plt.suptitle("Flame-graph benchmark (by tag)")
    plt.tight_layout(rect=[0, 0, 1, 0.95])  # 留出 suptitle 空间
    plt.savefig(outfile, format="svg")
    print(f"📊 Chart saved to {outfile}")


def show_tables(rows):
    # ── Rich 表格 ────────────────────────────────
    t = Table(title="Flame-graph Benchmark (mean, ms)", show_lines=True)
    t.add_column("Dataset", justify="right")
    t.add_column("perl", justify="right")
    t.add_column("inferno", justify="right")
    t.add_column("my_single", justify="right")
    t.add_column("my_parallel", justify="right")

    for row in rows:
        t.add_row(
            row["tag"],
            row.get("perl", "—"),
            row.get("inferno", "—"),
            row.get("my_single", "—"),
            row.get("my_parallel", "—"),
        )
    console.print(t)

    # ── Markdown 输出 ────────────
    md = [
        "| Dataset | perl | inferno | my_single | my_parallel |",
        "|--------:|------:|--------:|----------:|------------:|",
    ]
    for r in rows:
        md.append(
            f"| {r['tag']:>7} | {r.get('perl', '—'):>6} | {r.get('inferno', '—'):>8} | "
            f"{r.get('my_single', '—'):>10} | {r.get('my_parallel', '—'):>12} |"
        )

    with open("benchmark_result.md", "w") as f:
        f.write("\n".join(md))
        f.write("\n")

    console.print("✅ Markdown table saved to benchmark_result.md")


# ---------- 工具检测 ---------- #
def ensure_binary(prog: str) -> bool:
    return shutil.which(prog) is not None


def sanity_check():
    missing = []
    if not ensure_binary("hyperfine"):
        missing.append("hyperfine")
    if not ensure_binary("stackcollapse-perf.pl") or not ensure_binary("flamegraph.pl"):
        missing.append("FlameGraph perl scripts")
    if not ensure_binary("inferno-collapse-perf") or not ensure_binary("inferno-flamegraph"):
        missing.append("inferno tools")
    if missing:
        sys.exit(f"❌ dependency miss：{', '.join(missing)} — install it first")


# ---------- 生成 perf-script ---------- #
def gen_perf(samples: int):
    print(f"📦 Generate {samples:,} samples …")
    subprocess.run(
        ["python3", str(DATA_GEN), "--samples", str(samples), "--output", PERF_FILE],
        check=True,
    )


# ---------- run hyperfine ---------- #
def run_hyperfine(tag: str):
    json_out = f"benchmark_{tag}.json"
    cmd_list = []
    for name, tmpl in TOOLS.items():
        svg = f"{tag}_{name}.svg"
        if "|" in tmpl or ">" in tmpl:
            cmd_list.append(f"sh -c '{tmpl.format(input=PERF_FILE, output=svg)}'")
        else:
            cmd_list.append(tmpl.format(input=PERF_FILE, output=svg))

    hyper_cmd = ["hyperfine", *HYPERFINE_COMMON, "--export-json", json_out, *cmd_list]
    print(f"🚀 hyperfine ({tag}) …")
    subprocess.run(hyper_cmd, check=True)
    return json_out


def mean_ms(result: dict) -> str:
    return f"{result['mean'] * 1000:.1f}"


def parse_json(js_file: str) -> Dict[str, float]:
    with open(js_file) as f:
        data = json.load(f)
    out = {}
    for item in data["results"]:
        cmd = item["command"]
        key = None
        for k in TOOLS:
            if k in cmd:
                key = k
                break
        if key:
            out[key] = mean_ms(item)
    return out


# ---------- 主入口 ---------- #
def main():
    sanity_check()

    summary_rows = []

    for tag, samples, disp in DATASETS:
        print("\n" + "=" * 60)
        print(f"🔬 Benchmark {disp} ({samples:,} samples)")
        print("=" * 60)
        gen_perf(samples)
        js = run_hyperfine(tag)
        res = parse_json(js)
        row = {"tag": disp, **res}
        summary_rows.append(row)

    print("\n🏁 All benchmark finished!\n")
    show_tables(summary_rows)
    save_bar_chart(summary_rows)


if __name__ == "__main__":
    main()
