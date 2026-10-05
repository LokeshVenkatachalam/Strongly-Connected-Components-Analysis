# Strongly Connected Components Analysis

Comprehensive benchmark and analysis of parallel SCC algorithms on large-scale graphs (up to 92 billion edges), using the [BGR binary graph format](https://github.com/hpc-heterogeneous-graph-algorithms/graph-format-converters) for high-performance I/O. The repository also preserves exact, iterative Tarjan, Gabow, Pearce, and Tarjan-Zwick CPU baselines.

## Overview

This repository benchmarks **4 parallel SCC algorithms** across **36 graphs** at 1, 32, 64, and 128 threads on a 144-core server with 377GB RAM:

| Algorithm | Paper | Year | Venue |
|-----------|-------|------|-------|
| [Wang et al.](algorithms/wang-etal/) | Parallel Strong Connectivity Based on Faster Reachability | 2023 | SIGMOD |
| [GBBS](algorithms/gbbs/) | Theoretically Efficient Parallel Graph Algorithms | 2018 | SPAA |
| [iSpan](algorithms/ispan/) | Parallel Identification of SCCs with Spanning Trees | 2018 | SC |
| [par-scc](algorithms/par-scc/) | On Fast Parallel Detection of SCCs in Small-World Graphs | 2013 | SC |

The [sequential baselines](algorithms/sequential-baselines/) provide
outgoing-CSR-only Tarjan, Gabow, Pearce, and Tarjan-Zwick implementations.
They are separate from the four parallel algorithms above and do not use a
GPU.

### Key Results

Wang et al. (2023) dominates, winning 23 of 36 graphs:

| Graph | par-scc | Wang et al. | Speedup |
|-------|--------:|------------:|--------:|
| it-2004 (1.2B edges) | 49.0s | 0.5s | **95×** |
| webbase-2001 (1.0B edges) | 86.9s | 1.4s | **64×** |
| sk-2005 (1.9B edges) | 24.0s | 0.8s | **31×** |
| twitter7 (1.5B edges) | 0.97s | 0.26s | **3.7×** |

→ See [Results.md](results/Results.md) for full analysis.

## Repository Structure

```
├── algorithms/           # SCC algorithm implementations (git subtrees)
│   ├── wang-etal/        # Wang et al. SIGMOD 2023
│   ├── gbbs/             # GBBS (ParAlg)
│   ├── ispan/            # iSpan SC 2018
│   ├── par-scc/          # par-scc SC 2013 (modified for GM_EDGE64)
│   └── sequential-baselines/
│       ├── include/scc/     # Reusable public C++ API
│       ├── src/             # One translation unit per algorithm
│       ├── exact_scc.cpp    # Recovered exact iterative Tarjan analyzer
│       ├── run_suite.py     # Editable multi-graph suite launcher
│       └── suite.example.json
├── tools/                # BGR-based benchmark tooling
│   ├── scc_analyzer.cpp  # par-scc analyzer with BGR loading
│   ├── bgr2scc.cpp       # BGR → algorithm format converter
│   ├── run_all.sh        # par-scc benchmark runner
│   ├── run_modern_scc.sh # Wang/GBBS/iSpan benchmark runner  
│   └── gen_report.py     # Report generator
├── results/              # Benchmark results
│   ├── Results.md        # Full analysis report
│   ├── par_scc_results.csv
│   ├── modern_scc_results.csv
│   ├── sequential_scc_historical.csv
│   └── exact_tarjan_l40s.csv
└── docs/                 # GitHub Pages documentation
```

## Quick Start

### Prerequisites

- **g++ 9+** with C++17 and OpenMP support
- **144 cores, 377GB RAM** (for full benchmark; smaller configs work on smaller graphs)
- Graphs in [BGR format](https://github.com/hpc-heterogeneous-graph-algorithms/graph-format-converters)

### Build All Algorithms

```bash
# Wang et al.
cd algorithms/wang-etal && git submodule update --init --recursive
cd src && make scc

# GBBS
cd algorithms/gbbs/benchmarks/StronglyConnectedComponents/RandomGreedyBGSS16
make

# iSpan
cd algorithms/ispan/src && make

# par-scc (with GM_EDGE64 for >2B edges)
cd algorithms/par-scc && make lib && make bin

# Exact sequential baselines and self-tests
make -C algorithms/sequential-baselines check
```

### Convert BGR and Run

```bash
# Build the BGR converter
cd tools && g++ -O3 -std=c++17 -fopenmp -I/path/to/bgr/src bgr2scc.cpp -o bgr2scc

# Convert a BGR graph to all formats (writes to /dev/shm)
./bgr2scc /path/to/graph.bgr

# Run each algorithm
PARLAY_NUM_THREADS=128 algorithms/wang-etal/src/scc /dev/shm/scc_wang.bin -local_reach -local_scc -t 1 -status
PARLAY_NUM_THREADS=128 algorithms/gbbs/.../StronglyConnectedComponents -rounds 1 -stats -b /dev/shm/scc_wang.bin
algorithms/ispan/src/ispan /dev/shm/scc_ispan_fw_beg.bin /dev/shm/scc_ispan_fw_csr.bin \
    /dev/shm/scc_ispan_bw_beg.bin /dev/shm/scc_ispan_bw_csr.bin 128 1 100 4 0.1 1

# CPU-only outgoing-CSR baselines; '-' skips label output
algorithms/sequential-baselines/scc_compare graph.bgr tarjan -
algorithms/sequential-baselines/scc_compare graph.bgr gabow -
algorithms/sequential-baselines/scc_compare graph.bgr pearce -
algorithms/sequential-baselines/scc_compare graph.bgr tarjan-zwick -

# Map and validate once, then run a selected ordered subset
algorithms/sequential-baselines/scc_benchmark graph.bgr \
  --algorithms tarjan,gabow,pearce,tarjan-zwick \
  --validation-threads 8

# Run editable graph/algorithm selections
cp algorithms/sequential-baselines/suite.example.json suite.json
# Edit graph_dir, graphs, and algorithms in suite.json.
python3 algorithms/sequential-baselines/run_suite.py \
  --config suite.json --build
```

### Run Full Benchmark

```bash
# All graphs through all algorithms at 1/32/64/128 threads
cd tools && bash run_modern_scc.sh
```

## Documentation

- **[Results & Analysis](results/Results.md)** — Full benchmark report with tables, timing, histograms, and insights
- **[Documentation](docs/)** — Build instructions, dependencies, and algorithm details

## Algorithms Compared

### Wang et al. (SIGMOD 2023) — ⭐ Recommended
Uses Vertical Granularity Control (VGC) to break BFS synchronization barriers. 6× faster than GBBS, up to 95× faster than par-scc on web graphs.

### GBBS (SPAA 2018)
Industry-standard parallel graph benchmark suite. Uses randomized greedy BGSS16 algorithm. Solid baseline, 400+ citations.

### iSpan (SC 2018)
Parallel spanning tree construction with relaxed synchronization. OpenMP-based with explicit thread count control. Limited to <2B edges (int32 types).

### par-scc (SC 2013)
Classic parallel SCC for small-world graphs. Trim + FW-BW decomposition. Shows poor scaling on web graphs with complex SCC structure.

### Exact sequential baselines
Iterative Tarjan, Gabow, Pearce, and Tarjan-Zwick implementations for
correctness and single-thread comparisons. They read checked BGR v2 directly,
use no transpose, and can emit canonical minimum-vertex labels for
byte-for-byte comparison.
See their [provenance and reproducibility notes](algorithms/sequential-baselines/README.md).

## License

Each algorithm retains its original license (see respective directories). Benchmark tools are provided as-is.

## Maintainer

Lokesh Venkatachalam
