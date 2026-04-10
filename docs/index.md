---
layout: default
title: Home
nav_order: 1
permalink: /
---

# SCC Analysis
{: .fs-9 .fw-700 }

Benchmarking parallel Strongly Connected Components algorithms on graphs with up to **92 billion edges**.
{: .fs-5 .fw-300 }

[View Results →]({{ site.baseurl }}/results/){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Build & Run →]({{ site.baseurl }}/documentation/){: .btn .btn-outline .fs-5 .mb-4 .mb-md-0 }

---

## Algorithms

4 parallel SCC implementations benchmarked at 1, 32, 64, and 128 threads on a **144-core / 377GB** machine.

| Algorithm | Year | Venue | Approach | Code | Wins |
|:----------|:-----|:------|:---------|:-----|-----:|
| **Wang et al.** | 2023 | SIGMOD | VGC + hash reachability | [ucrparlay/Parallel-Strong-Connectivity](https://github.com/ucrparlay/Parallel-Strong-Connectivity) | **23** |
| **iSpan** | 2018 | SC | Parallel spanning trees | [iHeartGraph/iSpan](https://github.com/iHeartGraph/iSpan) | 8 |
| **GBBS** | 2018 | SPAA | Randomized greedy BGSS16 | [ParAlg/gbbs](https://github.com/ParAlg/gbbs) | 3 |
| **par-scc** | 2013 | SC | Trim + FW-BW decomposition | [nrodia/par-scc](https://github.com/nrodia/par-scc) | 2 |

Win count = number of graphs (out of 36) where the algorithm achieved the fastest time at its best thread count.

---

## Headline: Wang et al. vs par-scc

The 2023 algorithm crushes the 2013 baseline — especially on web graphs where par-scc struggled:

| Graph | Edges | par-scc | Wang | Speedup |
|:------|------:|--------:|-----:|--------:|
| it-2004 | 1.2B | 49.0s | 0.5s | **95×** |
| webbase-2001 | 1.0B | 86.9s | 1.4s | **64×** |
| sk-2005 | 1.9B | 24.0s | 0.8s | **31×** |
| indochina-2004 | 194M | 6.6s | 0.2s | **28×** |
| uk-2005 | 936M | 12.1s | 0.6s | **20×** |
| twitter7 | 1.5B | 0.97s | 0.26s | **3.7×** |
| GAP-kron | 4.2B | 0.72s | 0.37s | **1.9×** |

---

## Thread Scaling

Wang et al. achieves **20–58× speedup** from 1 → 128 threads:

| Graph | Edges | 1 Thread | 128 Threads | Speedup |
|:------|------:|---------:|------------:|--------:|
| kmer_A2a | 361M | 67.6s | 1.2s | 58× |
| kmer_V1r | 465M | 75.4s | 1.3s | 56× |
| GAP-kron | 4.2B | 14.6s | 0.4s | 40× |
| com-Friendster | 3.6B | 24.8s | 0.6s | 43× |
| webbase-2001 | 1.0B | 49.7s | 1.4s | 37× |

par-scc shows **poor scaling on web graphs** (1–2× at 128T) due to sequential FW-BW bottleneck on millions of small SCCs.

---

## LV Favicon Concepts

Here are **5 favicon color options** based on the bold **LV monogram + SCC loop** idea.
The browser tab currently uses **Option 1** so you can compare it live.

<div class="favicon-grid">
  <div class="favicon-card active">
    <img src="{{ '/assets/favicons/lv-loop-midnight-emerald.svg' | relative_url }}" alt="Option 1 midnight emerald favicon">
    <strong>Option 1 — Midnight Emerald</strong>
    <code>#0B1F4D</code> + <code>#34D399</code>
    <p>Current favicon</p>
  </div>
  <div class="favicon-card">
    <img src="{{ '/assets/favicons/lv-loop-cobalt-mint.svg' | relative_url }}" alt="Option 2 cobalt mint favicon">
    <strong>Option 2 — Cobalt Mint</strong>
    <code>#163B74</code> + <code>#2DD4BF</code>
    <p>Brighter blue, softer mint loop</p>
  </div>
  <div class="favicon-card">
    <img src="{{ '/assets/favicons/lv-loop-indigo-lime.svg' | relative_url }}" alt="Option 3 indigo lime favicon">
    <strong>Option 3 — Indigo Lime</strong>
    <code>#1E1B4B</code> + <code>#84CC16</code>
    <p>More contrast with a lime accent</p>
  </div>
  <div class="favicon-card">
    <img src="{{ '/assets/favicons/lv-loop-slate-jade.svg' | relative_url }}" alt="Option 4 slate jade favicon">
    <strong>Option 4 — Slate Jade</strong>
    <code>#0F172A</code> + <code>#10B981</code>
    <p>Darkest, most understated version</p>
  </div>
  <div class="favicon-card">
    <img src="{{ '/assets/favicons/lv-loop-royal-cyan.svg' | relative_url }}" alt="Option 5 royal cyan favicon">
    <strong>Option 5 — Royal Cyan</strong>
    <code>#1D2D6C</code> + <code>#22D3EE</code>
    <p>Cooler accent with a brighter loop</p>
  </div>
</div>

---

## Graphs

**36 graphs** from two collections, all in [BGR format](https://github.com/hpc-heterogeneous-graph-algorithms/graph-format-converters):

| Category | Examples | Edge range |
|:---------|:---------|:-----------|
| Web crawls | sk-2005, uk-2005, webbase-2001 | 194M – 1.9B |
| Social networks | com-Friendster, twitter7, com-Orkut | 69M – 3.6B |
| Synthetic | GAP-kron, GAP-urand | 4.2B – 4.3B |
| Genomics | kmer_V1r, kmer_A2a | 361M – 465M |
| Road/mesh | road_usa, europe_osm, delaunay_n24 | 34M – 108M |
| Directed conversions | 14 graphs with random edge orientation | 17M – 5.8B |

5 graphs exceeding 366GB RAM were skipped (eu-2015, uk-2014, clueweb12, gsh-2015, AGATHA_2015).

---

## Quick Start

```bash
# Build Wang et al. (fastest)
cd algorithms/wang-etal && git submodule update --init --recursive
cd src && make scc

# Convert BGR graph → Wang binary format (via /dev/shm)
cd tools && g++ -O3 -std=c++17 -fopenmp -I<BGR_SRC> bgr2scc.cpp -o bgr2scc
./bgr2scc /path/to/graph.bgr

# Run SCC with 128 threads
PARLAY_NUM_THREADS=128 ./algorithms/wang-etal/src/scc \
  /dev/shm/scc_wang.bin -local_reach -local_scc -t 1 -status
```

See [Documentation]({{ site.baseurl }}/documentation/) for full build instructions for all 4 algorithms.

---

## Repository

```
algorithms/
  wang-etal/     ← SIGMOD 2023 (⭐ recommended)
  gbbs/          ← SPAA 2018
  ispan/         ← SC 2018
  par-scc/       ← SC 2013 (modified: GM_EDGE64)
tools/
  bgr2scc.cpp    ← BGR → algorithm format converter
  scc_analyzer.cpp, run scripts, gen_report.py
results/
  Results.md     ← Full 7-section analysis
  *.csv          ← Raw timing data
docs/            ← This site
```
