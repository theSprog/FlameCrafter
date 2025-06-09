![License](https://img.shields.io/badge/license-MIT-blue.svg)

A **C++17-powered** tool to generate beautiful, high-performance flamegraphs from `perf` or DTrace stack samples – **no more Perl**, no more slow [perl scripts](https://github.com/brendangregg/FlameGraph)!



## ✨ Key Features

✅ **Header-only C++ Library**  
No complex build steps or dependencies – just include and go!

✅ **Modern C++17 Implementation**  
Using `std::string_view`, `pmr`, parallel algorithms(todo), and efficient data structures.

✅ **Parallel Flamegraph Building**  
Powered by [Intel TBB](https://github.com/oneapi-src/oneTBB)(if TBB avaliable), scales well on multi-core machines.

✅ **Beautiful SVG/HTML Output**  
No need for ancient Perl scripts – produce clean, colorful, and scalable flamegraph visualizations.

✅ **Supports perf and DTrace**  
Parse stack samples from multiple sources and render instantly.

✅ **Open and Extensible**  
Easily integrate into your own tools or extend to support new profile formats.



## 📦 Installation

Since it's header-only, just copy the `include/` folder into your project:

```bash
cp -r include/ your_project/
````



## 🚀 Usage

The typical workflow:

- **Collect stack samples** (with `perf` or `dtrace`)
- **Generate parsed stacks** (with `perf script`, for example)
- **Build flamegraph**:

```cpp
#include "flamegraph.hpp"

using namespace flamegraph;
    
int main(int argc, char* argv[]) {
    FlameGraphConfig config;
    config.title = "Performance Test Flame Graph";
    config.interactive = true;
    config.write_folded_file = false;

    FlameGraphGenerator generator(config);
    generator.generate("perf.parsed", "my_flamegraph.svg");
    generator.generate("perf.parsed", "my_flamegraph.html");	// generate .html
    return 0;
}
```


🔥 **Parallel rendering** is enabled by default if `TBB` is available. All you need is to use `ParallelFlameGraphGenerator` instead if `FlameGraphGenerator`

```cpp
#include "parallel_flamegraph.hpp"

using namespace flamegraph;

int main(int argc, char* argv[]) {
    FlameGraphConfig config;
    config.title = "Performance Test Flame Graph";
    config.interactive = true;
    config.write_folded_file = false;

    ParallelFlameGraphGenerator generator(config);
    generator.generate("perf.parsed", "my_flamegraph.svg");
    generator.generate("perf.parsed", "my_flamegraph.html");	// generate .html
    return 0;
}
```

`ParallelFlameGraphGenerator` depends on `TBB` so do not forget to link with `tbb` by LINK_FLAG `-ltbb`



## ⚡ Performance

<div align="center">
  <img src="bench/benchmark_chart.svg">
</div>

| Samples | Perl   | inferno | FlameCrafter_Single |
|--------:|------: |--------: |----------:|
|      10 |   29.3 |      9.5 |        **4.1** |
|     100 |   50.5 |      9.3 |        **5.4** |
|     1 K |  213.7 |     **26.0** |       28.6 |
|    10 K | 1779.1 |    **192.2** |      219.9 |
|   100 K | 7293.8 |    479.5 |      **399.1** |
|     1 M | 93163.8 |   8335.6 |     **4831.5** |


Although not faster than [inferno](https://github.com/jonhoo/inferno) in some middle dataset(Parallel is not offered now, `JUST TODO`), **FlameCrafter** offers:

* 🏗️ **Header-only simplicity** – no build, no external runtime
* 🎯 **C++17 efficiency** – tight memory usage and zero-cost abstractions
* 🔌 **Easily embeddable** – integrate directly into your C++ projects



## 📜 License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
