---
layout: default
title: Documentation
nav_order: 2
permalink: /documentation/
---

# Documentation
{: .fs-8 }

Build instructions, dependencies, and usage guide for the SCC benchmark suite.
{: .fs-5 .fw-300 }

---

## Prerequisites

| Requirement | Minimum | Recommended |
|:------------|:--------|:------------|
| **g++** | 9+ (C++17) | 11+ |
| **OpenMP** | 3.0 | 4.5+ |
| **RAM** | 16GB (small graphs) | 377GB (full suite) |
| **CPU cores** | 1 | 128–144 |
| **Python 3** | 3.6+ | 3.8+ (for report generation) |
| **Graph format** | [BGR](https://github.com/hpc-heterogeneous-graph-algorithms/graph-format-converters) | — |

```bash
# Ubuntu/Debian
sudo apt install g++ python3 numactl
```

---

## Building the Algorithms

### Wang et al. (SIGMOD 2023)

```bash
cd algorithms/wang-etal
git submodule update --init --recursive
cd src && make scc
```

**Thread control:** `PARLAY_NUM_THREADS=N ./scc <graph> [options]`

**Options:**
- `-local_reach` — Enable VGC for reachability (recommended)
- `-local_scc` — Enable VGC for multi-search (recommended)
- `-t N` — Number of timing repetitions
- `-status` — Print SCC count and largest SCC size
- `-beta X` — Exponential growth rate (default: 1.5)

### GBBS (SPAA 2018)

```bash
cd algorithms/gbbs
cd benchmarks/StronglyConnectedComponents/RandomGreedyBGSS16
make                  # Default: 64-bit nodes, 32-bit edges
make EDGELONG=1       # For >2B edges (64-bit edges)
```

**Thread control:** `PARLAY_NUM_THREADS=N ./StronglyConnectedComponents [options] <graph>`

**Options:**
- `-b` — Binary format input (required for our converted files)
- `-rounds N` — Number of timing repetitions
- `-stats` — Print SCC statistics

### iSpan (SC 2018)

```bash
cd algorithms/ispan/src && make
```

**Usage:**
```bash
./ispan <fw_beg> <fw_csr> <bw_beg> <bw_csr> <threads> <alpha> <beta> <gamma> <theta> <runs>
```

**Default parameters:** `alpha=1 beta=100 gamma=4 theta=0.1 runs=1`

{: .warning }
> iSpan uses `int` (32-bit) for edge indices. Graphs with >2.1 billion edges will overflow. To fix, change `typedef int index_t` to `typedef long index_t` in `src/util.h`.

### par-scc (SC 2013)

```bash
cd algorithms/par-scc
make lib    # Build gm_graph library (with GM_EDGE64)
make bin    # Build SCC binary
```

**Usage:** `./scc <graph_name> <num_threads> <method> {-d|-a|-p}`

**Methods:** 0 (Trim+FW-BW), 1 (Global FW-BW), 2 (Full pipeline), 3 (Tarjan), 4 (Trim+Tarjan)

### Exact Sequential Baselines

The repository includes outgoing-CSR-only, iterative Tarjan, Gabow, Pearce,
and Tarjan-Zwick implementations. They read checked BGR v2 directly, require
one CPU thread, and do not build a transpose or use a GPU.

```bash
# Build and run the exhaustive correctness tests
make -C algorithms/sequential-baselines check

# Write one canonical uint32 label per vertex
algorithms/sequential-baselines/scc_compare \
  graph.bgr tarjan tarjan.labels.bin 1800
algorithms/sequential-baselines/scc_compare \
  graph.bgr gabow gabow.labels.bin 1800
algorithms/sequential-baselines/scc_compare \
  graph.bgr pearce pearce.labels.bin 1800
algorithms/sequential-baselines/scc_compare \
  graph.bgr tarjan-zwick tarjan-zwick.labels.bin 1800

# Use '-' to skip label output
algorithms/sequential-baselines/scc_compare graph.bgr tarjan -

# Recovered exact Tarjan analyzer with additional verification
algorithms/sequential-baselines/exact_scc graph.bgr
```

The optional final argument is the SCC-phase time limit in seconds. Labels use
the minimum vertex ID in each component, so all four outputs can be compared
byte-for-byte.

The checked reader accepts 32/64-bit BGR edge offsets and weighted files,
validates all row ends and destination IDs, and ignores weights. Edge counts
remain 64-bit. Tarjan-Zwick supports up to `2^31 - 1` vertices because it uses
the high vertex-ID bit as a stack flag; Pearce supports fewer than
`2^32 - 2`; all current PHEM graphs are below `2^30`.

#### Load once, run several algorithms

```bash
algorithms/sequential-baselines/scc_benchmark graph.bgr \
  --algorithms tarjan,gabow,pearce,tarjan-zwick \
  --validation-threads 8 \
  --time-limit 1800 \
  --labels-dir labels/graph
```

The graph is mapped and fully validated once. Each algorithm receives the same
read-only graph view and allocates an independent workspace that is released
before the next run. Current algorithms are outgoing-only, so the runner skips
transpose construction. Validation can use all CPUs assigned because of the
memory request; the four SCC kernels remain single-threaded.

For an editable graph/algorithm campaign:

```bash
cp algorithms/sequential-baselines/suite.example.json suite.json
# Edit graph_dir, graphs, and algorithms.
python3 algorithms/sequential-baselines/run_suite.py \
  --config suite.json --build
```

The implementation is a reusable static library with one source file per
algorithm under `src/` and public headers under `include/scc/`.

See the
[source and provenance notes](https://github.com/LokeshVenkatachalam/Strongly-Connected-Components-Analysis/tree/main/algorithms/sequential-baselines)
and the [measured sequential runtimes]({{ site.baseurl }}/results/#8-exact-sequential-baselines-tarjan-and-gabow).

---

## BGR Format Conversion

The `bgr2scc` tool loads a BGR graph once and writes all algorithm formats to `/dev/shm` (RAM-backed tmpfs):

```bash
cd tools
g++ -O3 -std=c++17 -fopenmp -I<BGR_SRC_PATH> bgr2scc.cpp -o bgr2scc

# Convert (loads BGR with 64-thread parallel I/O)
./bgr2scc /path/to/graph.bgr
```

**Output files in `/dev/shm/`:**

| File | Format | Used by |
|:-----|:-------|:--------|
| `scc_wang.bin` | `[n:u64][m:u64][sizes:u64][offset:u64*(n+1)][edges:u32*m]` | Wang, GBBS |
| `scc_ispan_fw_beg.bin` | `int32[n+1]` — forward CSR offsets | iSpan |
| `scc_ispan_fw_csr.bin` | `int32[m]` — forward adjacency | iSpan |
| `scc_ispan_bw_beg.bin` | `int32[n+1]` — reverse CSR offsets | iSpan |
| `scc_ispan_bw_csr.bin` | `int32[m]` — reverse adjacency | iSpan |

---

## Running the Full Benchmark

### Modern Algorithms (Wang, GBBS, iSpan)

```bash
cd tools
# Edit run_modern_scc.sh to set paths to algorithm binaries and graph directories
bash run_modern_scc.sh
```

This processes each graph through all algorithms at 1/32/64/128 threads and writes `modern_scc_results.csv`.

### par-scc Baseline

```bash
cd tools
# Build the par-scc analyzer first (requires gm_graph library)
bash run_all.sh
```

### Generate Report

```bash
python3 gen_report.py
```

---

## Graph Format

All graphs use the **BGR (Binary Graph Representation) format v2** — a compact CSR binary format supporting:
- Weighted and unweighted graphs
- Adaptive integer sizes (uint32/uint64)
- Parallel I/O with `pread()`/`pwrite()`

See [BGR format documentation](https://github.com/hpc-heterogeneous-graph-algorithms/graph-format-converters) for details.
