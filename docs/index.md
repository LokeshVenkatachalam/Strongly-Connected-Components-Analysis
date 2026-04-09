---
layout: default
title: Home
nav_order: 1
permalink: /
---

# Strongly Connected Components Analysis
{: .fs-9 }

Comprehensive benchmark of parallel SCC algorithms on large-scale graphs (up to 92 billion edges).
{: .fs-6 .fw-300 }

[View Results]({{ site.baseurl }}/results/){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Documentation]({{ site.baseurl }}/documentation/){: .btn .fs-5 .mb-4 .mb-md-0 }

---

## Overview

This project benchmarks **4 parallel SCC algorithms** across **36 graphs** at 1, 32, 64, and 128 threads on a 144-core server with 377GB RAM, using the [BGR binary graph format](https://github.com/hpc-heterogeneous-graph-algorithms/graph-format-converters) for high-performance I/O.

### Algorithms Compared

| Algorithm | Year | Venue | Win Count (of 36) |
|:----------|:-----|:------|-------------------:|
| **[Wang et al.](https://github.com/ucrparlay/Parallel-Strong-Connectivity)** | 2023 | SIGMOD | **23** |
| [iSpan](https://github.com/iHeartGraph/iSpan) | 2018 | SC | 8 |
| [GBBS](https://github.com/ParAlg/gbbs) | 2018 | SPAA | 3 |
| [par-scc](https://github.com/nrodia/par-scc) | 2013 | SC | 2 |

### Headline Results

Wang et al. (2023) is **10–95× faster than par-scc (2013)** on web graphs:

| Graph | Edges | par-scc (128T) | Wang (128T) | Speedup |
|:------|------:|---------------:|------------:|--------:|
| it-2004 | 1.2B | 49.0s | 0.5s | **95×** |
| webbase-2001 | 1.0B | 86.9s | 1.4s | **64×** |
| sk-2005 | 1.9B | 24.0s | 0.8s | **31×** |
| indochina-2004 | 194M | 6.6s | 0.2s | **28×** |
| uk-2005 | 936M | 12.1s | 0.6s | **20×** |

### Thread Scaling

Wang et al. achieves **20–58× speedup** from 1→128 threads on large graphs:
- kmer_A2a (361M edges): 67.6s → 1.2s (58×)
- kmer_V1r (465M edges): 75.4s → 1.3s (56×)
- GAP-kron (4.2B edges): 14.6s → 0.4s (40×)

---

## Repository Structure

```
├── algorithms/           # SCC implementations (git subtrees)
│   ├── wang-etal/        # Wang et al. SIGMOD 2023
│   ├── gbbs/             # GBBS (ParAlg)
│   ├── ispan/            # iSpan SC 2018
│   └── par-scc/          # par-scc SC 2013 (modified for GM_EDGE64)
├── tools/                # BGR-based benchmark tooling
├── results/              # Benchmark results and analysis
└── docs/                 # This documentation site
```

---

## Graph Datasets

36 graphs benchmarked from two collections:

- **`/ssd/Graphs/bgr/`** — 22 original graphs (web crawls, social networks, road networks, synthetic)
- **`/ssd/Graphs/bgr_directed/`** — 14 directed versions of originally undirected graphs

Graphs range from 1,677 edges (GD96_a) to 4.3 billion edges (GAP-urand), with 5 graphs exceeding 366GB RAM skipped.

---

## Maintainer

[Lokesh Venkatachalam](https://github.com/LokeshVenkatachalam)
