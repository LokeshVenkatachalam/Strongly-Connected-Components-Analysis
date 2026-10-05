# Exact sequential SCC baselines

This directory preserves CPU-only, iterative SCC baselines used while
investigating the large PHEM graphs. None of the implementations uses CUDA or
requires a GPU.

## Files

- `exact_scc.cpp` is the byte-for-byte recovered exact Tarjan analyzer from the
  L40S worktree. It validates BGR v2 input, computes SCCs iteratively, and
  performs additional reachability and condensation checks.
- `src/tarjan.cpp`, `src/gabow.cpp`, `src/pearce.cpp`, and
  `src/tarjan_zwick.cpp` are independently compiled algorithm
  implementations.
- `include/scc/` contains the public graph, algorithm, result, and I/O APIs.
- `src/graph.cpp` owns checked, read-only BGR mapping and validation.
- `scc_compare` runs one selected algorithm for backward compatibility.
- `scc_benchmark` maps and validates one graph once, then runs any ordered
  subset of the four algorithms against that same mapping.
- `run_suite.py` selects graphs and algorithms from command-line options or an
  editable JSON configuration.
- `SCIPY_LICENSE.txt` contains the BSD 3-Clause license covering the SciPy
  Pearce and Tarjan-Zwick implementations from which the C++ ports were
  adapted.

All four comparison algorithms use only outgoing CSR. They do not build a
transpose.

## Build and test

```bash
make -C algorithms/sequential-baselines check
```

The Makefile builds each implementation into its own object file, archives the
objects as `build/libscc.a`, and links separate CLI, benchmark, and self-test
executables. Header dependencies are tracked automatically.

The test target:

1. checks all four algorithms against a transitive-closure oracle for all
   65,536 directed graphs on four vertices;
2. checks 2,048 deterministic random graphs with 5–16 vertices plus duplicate
   edges;
3. checks a 200,000-vertex directed chain and cycle to ensure all
   implementations remain iterative;
4. runs the recovered Tarjan analyzer's checked 64-bit arithmetic test.

## Run

```bash
# Write one uint32 little-endian canonical label per vertex.
algorithms/sequential-baselines/scc_compare \
  graph.bgr tarjan tarjan.labels.bin 1800

algorithms/sequential-baselines/scc_compare \
  graph.bgr gabow gabow.labels.bin 1800

algorithms/sequential-baselines/scc_compare \
  graph.bgr pearce pearce.labels.bin 1800

algorithms/sequential-baselines/scc_compare \
  graph.bgr tarjan-zwick tarjan-zwick.labels.bin 1800

# Use '-' or omit the label path to skip label output.
algorithms/sequential-baselines/scc_compare graph.bgr gabow -

# The recovered Tarjan analyzer emits a JSON analysis record.
algorithms/sequential-baselines/exact_scc graph.bgr
```

Each comparison label is the minimum vertex ID in that vertex's SCC. Therefore
all four outputs can be compared byte-for-byte even though the algorithms
discover components in different orders.

## Load once and run a selected suite

```bash
algorithms/sequential-baselines/scc_benchmark graph.bgr \
  --algorithms tarjan,gabow,pearce,tarjan-zwick \
  --validation-threads 8 \
  --time-limit 1800 \
  --labels-dir labels/graph
```

The benchmark executable:

1. opens, maps, and validates the BGR once;
2. retains that read-only mapping for the complete process;
3. runs algorithms in the requested order with independent workspaces;
4. frees each workspace before starting the next algorithm;
5. optionally writes canonical labels;
6. emits one `BGR_LOAD`, one `SCC_RESULT` per algorithm, and one
   `BENCHMARK_RESULT` JSON record.

`--validation-threads` parallelizes only the shared row/end-point validation
pass. Every SCC implementation remains single-threaded, preserving the
sequential comparison boundary. This uses CPUs that Slurm may allocate because
of the job's memory request instead of leaving them idle.

All current algorithms need only outgoing CSR, so no transpose is constructed.
Preprocessing is intentionally dependency-driven rather than paying for an
unused transpose. A future algorithm that requires reverse CSR should add a
shared preprocessor and mark that requirement in the algorithm registry.

For multiple editable graphs:

```bash
cp algorithms/sequential-baselines/suite.example.json suite.json
# Edit graph_dir, graphs, and algorithms in suite.json.
python3 algorithms/sequential-baselines/run_suite.py \
  --config suite.json --build

# A positional graph name overrides the config graph list.
python3 algorithms/sequential-baselines/run_suite.py clueweb12 \
  --graph-dir /scratch/lokesh.v/phem-graphs/all-bgr \
  --algorithms tarjan-zwick,pearce
```

`run_suite.py` starts one `scc_benchmark` process per graph, so every selected
algorithm for that graph shares one BGR mapping and one validation pass. It
writes a separate combined log for each graph and fails explicitly on the first
unsuccessful run.

## BGR and execution boundaries

- Linux on a little-endian 64-bit host is required.
- BGR v2 `uint32` and `uint64` node-count fields are accepted when the actual
  vertex count fits the algorithm's limit.
- Both 32-bit and 64-bit cumulative edge offsets are accepted; edge counts and
  file positions remain 64-bit throughout.
- Weighted BGR files are accepted. The float weight tail is size-validated and
  ignored because SCC depends only on graph structure.
- Row ends, every destination ID, and the complete file size are validated
  before timing the SCC phase.
- The input is mapped read-only and checked for concurrent size/timestamp
  changes before and after the solve.
- Canonical label output is `uint32` little-endian and is published atomically.

Algorithm-specific vertex limits:

| Algorithm | Maximum supported vertices | Reason |
|---|---:|---|
| Tarjan | `2^32 - 1` | `uint32` vertex IDs and a separate sentinel |
| Gabow | `2^32 - 1` | `uint32` vertex IDs and a separate sentinel |
| Pearce | fewer than `2^32 - 2` | Two reserved stack-link sentinels |
| Tarjan-Zwick | `2^31 - 1` | The high vertex-ID bit encodes the lead flag |

All current PHEM inputs have fewer than `2^30` vertices and satisfy every
limit. The optional time limit applies only to workspace initialization and
the SCC phase; BGR validation and label writing are reported separately.

## SciPy algorithm ports

The Pearce implementation is adapted from SciPy 1.17.0
`scipy/sparse/csgraph/_traversal.pyx`, which cites:

> D. J. Pearce, "An Improved Algorithm for Finding the Strongly Connected
> Components of a Directed Graph", Technical Report, 2005.

The Tarjan-Zwick implementation is adapted from current SciPy
`_traversal.pyx`, which uses the improvements described in:

> Robert E. Tarjan and Uri Zwick, "Finding strong components using
> depth-first search", European Journal of Combinatorics 119 (2024),
> [doi:10.1016/j.ejc.2023.103815](https://doi.org/10.1016/j.ejc.2023.103815).

The C++ adaptations replace NumPy/Cython storage with checked BGR access,
64-bit edge positions, iterative fixed-capacity stacks, deadlines, progress
records, and canonical labels.

| SciPy source | Revision | Downloaded file SHA-256 |
|---|---|---|
| Pearce-era `_traversal.pyx` | `scipy/scipy` tag `v1.17.0`, commit `e2f9e521194c114923a9ec47409c14ff1898334a` | `4f78f626ad9c896620429f5753c90dae9f24b77bc450c561d28b58971aba1b35` |
| Tarjan-Zwick `_traversal.pyx` | `scipy/scipy` commit `8b4175a6cbba602362234f90fe36bee4800d36b7` | `e32d5d6a59fbe4705a10e1c95526a3896be485db3dacc9f0ce67f7e83224117c` |

SciPy's required copyright notice, conditions, and disclaimer are preserved in
`SCIPY_LICENSE.txt`.

## Provenance and reproducibility boundary

The recovered `exact_scc.cpp` has SHA-256:

```text
050f7329d1207fc292890db0971083f133e506866c1b2c8d13d272da9cf5cbff
```

It was recovered from:

```text
/home/lokesh/github/lokeshvenkatachalam/
  PageRank-scc-structure-20260928/benchmark/structure/scc/exact_scc.cpp
```

The earlier Tarjan/Gabow comparison source was untracked and had already been
deleted before this repository addition. The surviving experiment manifest
records these original hashes:

| Artifact | SHA-256 |
|---|---|
| `main.cpp` | `cbd1a275b147c161b207a3282ce47a2aa6e427b4d3c1d06650460dc688d09dc1` |
| `scc_algorithms.hxx` | `2809b1505495819f09645994ac530f5a369af1947bd8fade0b5de8f101da84a0` |
| `run.py` | `716d59a984eb6899346ddd3fb6bc7d4ab271dd048e33961f086b7bea90d680cd` |
| `scc_compare` binary | `9f83aba74bb9a9d528f26204300e70b195da562442968c1be03f4321ad8aed7d` |

`src/tarjan.cpp` and `src/gabow.cpp` are new, independently tested
reconstructions. Their behavior matches the preserved interface and
canonical-label contract, but their source hashes are intentionally not
presented as the deleted implementation. Historical SK-2005 and UK-2014 timings in
`results/sequential_scc_historical.csv` belong to the preserved binary hash
above and must not be attributed to the reconstruction without rerunning them.

The separate recursive Tarjan baseline distributed with Wang et al. remains at
`../wang-etal/baselines/tarjan_scc/`. It came from
`ucrparlay/Parallel-Strong-Connectivity` commit
`21550b0` and was not used for the iterative comparison timings.
