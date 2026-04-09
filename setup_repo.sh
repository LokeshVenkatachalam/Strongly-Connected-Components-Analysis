#!/bin/bash
# setup_repo.sh — Run this script locally where you have GitHub push access
# It recreates the full repo structure with subtrees and pushes to GitHub
set -euo pipefail

REPO="https://github.com/LokeshVenkatachalam/Strongly-Connected-Components-Analysis.git"
SRC="/hdd/lokeshvenkatachalam/Strongly-Connected-Components-Analysis"

echo "=== Setting up Strongly-Connected-Components-Analysis ==="

cd "$SRC"
git checkout -b setup-scc-analysis 2>/dev/null || git checkout setup-scc-analysis

# Push the branch with all commits
git push origin setup-scc-analysis

# Create PR
echo ""
echo "=== Creating Pull Request ==="
echo "Branch 'setup-scc-analysis' pushed."
echo ""
echo "Create PR at: https://github.com/LokeshVenkatachalam/Strongly-Connected-Components-Analysis/compare/main...setup-scc-analysis"
echo ""
echo "PR Title: Add SCC algorithm benchmarks, tools, results, and GitHub Pages"
echo ""
echo "PR Body:"
cat << 'PRBODY'
## Summary

Comprehensive SCC benchmark infrastructure with 4 parallel algorithms, BGR-based tooling, and GitHub Pages documentation.

### Changes

**Algorithm subtrees:**
- `algorithms/wang-etal/` — Wang et al. SIGMOD 2023 (Parallel Strong Connectivity)
- `algorithms/gbbs/` — GBBS (ParAlg, SPAA 2018)
- `algorithms/ispan/` — iSpan (SC 2018)
- `algorithms/par-scc/` — par-scc (SC 2013, modified for GM_EDGE64)

**Benchmark tools (`tools/`):**
- `scc_analyzer.cpp` — par-scc based SCC analyzer with BGR loading
- `bgr2scc.cpp` — BGR → algorithm format converter
- `run_modern_scc.sh` — Full benchmark runner (Wang/GBBS/iSpan)
- `gen_report.py` — Report generator

**Results (`results/`):**
- `Results.md` — Full analysis (7 sections, 36 graphs, 4 algorithms)
- `par_scc_results.csv` — par-scc timing data
- `modern_scc_results.csv` — Wang/GBBS/iSpan timing data

**Documentation (`docs/`):**
- GitHub Pages with Just the Docs theme
- Landing page, documentation, and results pages
- Deploys to lokeshvenkatachalam.github.io/Strongly-Connected-Components-Analysis

### Key Results

Wang et al. (2023) wins 23 of 36 graphs, with 10-95× speedup over par-scc on web graphs.
PRBODY

echo ""
echo "Done! Go create the PR on GitHub."
