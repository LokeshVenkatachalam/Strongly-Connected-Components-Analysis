# Exact sequential SCC baselines

This directory preserves the CPU-only, iterative SCC baselines used while
investigating the large PHEM graphs. Neither implementation uses CUDA or
requires a GPU.

## Files

- `exact_scc.cpp` is the byte-for-byte recovered exact Tarjan analyzer from the
  L40S worktree. It validates BGR v2 input, computes SCCs iteratively, and
  performs additional reachability and condensation checks.
- `scc_compare.cpp` is a standalone reconstruction of the deleted comparison
  program. It provides iterative Tarjan and Gabow path-based SCC algorithms,
  canonical labels, checked BGR v2 input, optional atomic label-file output,
  progress records, and a time limit.

Both algorithms use only outgoing CSR. They do not build a transpose.

## Build and test

```bash
make -C algorithms/sequential-baselines check
```

The test target:

1. checks Tarjan and Gabow against a transitive-closure oracle for all 65,536
   directed graphs on four vertices;
2. checks a 200,000-vertex directed chain and cycle to ensure both
   implementations remain iterative;
3. runs the recovered Tarjan analyzer's checked 64-bit arithmetic test.

## Run

```bash
# Write one uint32 little-endian canonical label per vertex.
algorithms/sequential-baselines/scc_compare \
  graph.bgr tarjan tarjan.labels.bin 1800

algorithms/sequential-baselines/scc_compare \
  graph.bgr gabow gabow.labels.bin 1800

# Use '-' or omit the label path to skip label output.
algorithms/sequential-baselines/scc_compare graph.bgr gabow -

# The recovered Tarjan analyzer emits a JSON analysis record.
algorithms/sequential-baselines/exact_scc graph.bgr
```

Each comparison label is the minimum vertex ID in that vertex's SCC. Therefore
Tarjan and Gabow outputs can be compared byte-for-byte even though the
algorithms discover components in different orders.

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

`scc_compare.cpp` is a new, independently tested reconstruction. Its behavior
matches the preserved interface and canonical-label contract, but its source
hash is intentionally not presented as the deleted implementation. Historical
SK-2005 and UK-2014 timings in
`results/sequential_scc_historical.csv` belong to the preserved binary hash
above and must not be attributed to the reconstruction without rerunning them.

The separate recursive Tarjan baseline distributed with Wang et al. remains at
`../wang-etal/baselines/tarjan_scc/`. It came from
`ucrparlay/Parallel-Strong-Connectivity` commit
`21550b0` and was not used for the iterative comparison timings.
