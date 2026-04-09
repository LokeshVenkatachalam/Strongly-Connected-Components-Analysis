#!/usr/bin/env python3
"""Generate sccreport.md from results.csv"""
import csv, os

CSV = os.path.join(os.path.dirname(__file__), "results.csv")
OUT = os.path.join(os.path.dirname(__file__), "sccreport.md")

def fmt(n):
    try: return f"{int(n):,}"
    except: return str(n)

def fmt_time(ms):
    try:
        v = float(ms)
        if v >= 1000: return f"{v/1000:.1f}s"
        return f"{v:.1f}ms"
    except: return ms

rows = []
with open(CSV) as f:
    reader = csv.DictReader(f)
    for r in reader:
        rows.append(r)

bgr = [r for r in rows if r['source'] == 'bgr' and r['numSCCs'] != 'SKIPPED']
bgr_skip = [r for r in rows if r['source'] == 'bgr' and r['numSCCs'] == 'SKIPPED']
bgr_dir = [r for r in rows if r['source'] == 'bgr_directed']

with open(OUT, 'w') as f:
    f.write("# SCC Analysis Report\n\n")
    f.write("Parallel SCC analysis using [par-scc](https://github.com/nrodia/par-scc) ")
    f.write("(Method 1: Trim1 + Global FW-BW + Trim1 + FW-BW) with Kosaraju for correctness verification.\n\n")
    f.write(f"- **Graphs analyzed:** {len(bgr) + len(bgr_dir)} ({len(bgr)} from bgr, {len(bgr_dir)} from bgr_directed)\n")
    f.write(f"- **Skipped (exceeds 366GB RAM):** {len(bgr_skip)} ({', '.join(r['graph'] for r in bgr_skip)})\n")
    f.write(f"- **Hardware:** 144 cores, 377GB RAM; graph loaded once with 64 threads\n")
    f.write(f"- **SCC timed at:** 1, 32, 64, 128 threads\n\n")

    # ===== BGR graphs =====
    f.write("## 1. Original Graphs (`/ssd/Graphs/bgr`)\n\n")
    f.write("### SCC Summary\n\n")
    f.write("| # | Graph | Nodes | Edges | SCCs | WCCs | Largest SCC Nodes | Largest SCC Edges | Largest SCC % |\n")
    f.write("|---|-------|------:|------:|-----:|-----:|------------------:|------------------:|--------------:|\n")
    for i, r in enumerate(sorted(bgr, key=lambda x: -int(x['edges'])), 1):
        N, M = int(r['nodes']), int(r['edges'])
        ln = int(r['largestSCC_nodes'])
        le = int(r['largestSCC_edges'])
        pct = f"{ln/N*100:.2f}%" if N > 0 else "0%"
        f.write(f"| {i} | {r['graph']} | {fmt(N)} | {fmt(M)} | {fmt(r['numSCCs'])} | {fmt(r['numWCCs'])} | {fmt(ln)} | {fmt(le)} | {pct} |\n")

    f.write("\n### SCC Timing (ms)\n\n")
    f.write("| Graph | Edges | Load | 1 Thread | 32 Threads | 64 Threads | 128 Threads | Speedup (1→128) |\n")
    f.write("|-------|------:|-----:|---------:|-----------:|-----------:|------------:|----------------:|\n")
    for r in sorted(bgr, key=lambda x: -int(x['edges'])):
        t1 = float(r['scc_1t_ms'])
        t128 = float(r['scc_128t_ms'])
        speedup = f"{t1/t128:.1f}x" if t128 > 0 else "N/A"
        f.write(f"| {r['graph']} | {fmt(r['edges'])} | {fmt_time(r['load_ms'])} | {fmt_time(r['scc_1t_ms'])} | {fmt_time(r['scc_32t_ms'])} | {fmt_time(r['scc_64t_ms'])} | {fmt_time(r['scc_128t_ms'])} | {speedup} |\n")

    f.write("\n### SCC Size Histograms\n\n")
    for r in sorted(bgr, key=lambda x: -int(x['edges'])):
        f.write(f"**{r['graph']}** ({fmt(r['numSCCs'])} SCCs):\n\n")
        for item in r['histogram'].split('; '):
            bucket, count = item.split(':')
            f.write(f"- {bucket}: {fmt(count)}\n")
        f.write("\n")

    # ===== BGR_DIRECTED graphs =====
    f.write("## 2. Directed Graphs (`/ssd/Graphs/bgr_directed`)\n\n")
    f.write("These are the undirected graphs from bgr/ converted to directed by randomly assigning one direction per edge.\n\n")
    f.write("### SCC Summary\n\n")
    f.write("| # | Graph | Nodes | Edges | SCCs | WCCs | Largest SCC Nodes | Largest SCC Edges | Largest SCC % |\n")
    f.write("|---|-------|------:|------:|-----:|-----:|------------------:|------------------:|--------------:|\n")
    for i, r in enumerate(sorted(bgr_dir, key=lambda x: -int(x['edges'])), 1):
        N = int(r['nodes'])
        ln = int(r['largestSCC_nodes'])
        le = int(r['largestSCC_edges'])
        pct = f"{ln/N*100:.2f}%" if N > 0 else "0%"
        f.write(f"| {i} | {r['graph']} | {fmt(N)} | {fmt(r['edges'])} | {fmt(r['numSCCs'])} | {fmt(r['numWCCs'])} | {fmt(ln)} | {fmt(le)} | {pct} |\n")

    f.write("\n### SCC Timing (ms)\n\n")
    f.write("| Graph | Edges | Load | 1 Thread | 32 Threads | 64 Threads | 128 Threads | Speedup (1→128) |\n")
    f.write("|-------|------:|-----:|---------:|-----------:|-----------:|------------:|----------------:|\n")
    for r in sorted(bgr_dir, key=lambda x: -int(x['edges'])):
        t1 = float(r['scc_1t_ms'])
        t128 = float(r['scc_128t_ms'])
        speedup = f"{t1/t128:.1f}x" if t128 > 0 else "N/A"
        f.write(f"| {r['graph']} | {fmt(r['edges'])} | {fmt_time(r['load_ms'])} | {fmt_time(r['scc_1t_ms'])} | {fmt_time(r['scc_32t_ms'])} | {fmt_time(r['scc_64t_ms'])} | {fmt_time(r['scc_128t_ms'])} | {speedup} |\n")

    f.write("\n### SCC Size Histograms\n\n")
    for r in sorted(bgr_dir, key=lambda x: -int(x['edges'])):
        f.write(f"**{r['graph']}** ({fmt(r['numSCCs'])} SCCs):\n\n")
        for item in r['histogram'].split('; '):
            bucket, count = item.split(':')
            f.write(f"- {bucket}: {fmt(count)}\n")
        f.write("\n")

    # ===== Skipped =====
    if bgr_skip:
        f.write("## 3. Skipped Graphs\n\n")
        f.write("These graphs exceed available memory (366GB RAM) for full SCC analysis.\n\n")
        f.write("| Graph | Nodes | Edges | Est. Memory |\n")
        f.write("|-------|------:|------:|------------:|\n")
        for r in bgr_skip:
            N, M = int(r['nodes']), int(r['edges'])
            mem = ((N+1)*8*2 + M*4*2 + M*8*2 + N*4*6) / (1024**3)
            f.write(f"| {r['graph']} | {fmt(N)} | {fmt(M)} | {mem:.0f} GB |\n")

    # ===== Insights =====
    f.write("\n## 4. Insights\n\n")
    
    f.write("### Parallel Speedup\n\n")
    f.write("The par-scc algorithm shows strong parallel scaling on large graphs:\n\n")
    # Find best speedups
    all_completed = bgr + bgr_dir
    speedups = []
    for r in all_completed:
        t1, t128 = float(r['scc_1t_ms']), float(r['scc_128t_ms'])
        if t128 > 10:  # skip tiny
            speedups.append((r['graph'], r['source'], t1, t128, t1/t128))
    speedups.sort(key=lambda x: -x[4])
    f.write("**Top 5 speedups (1→128 threads):**\n")
    for name, src, t1, t128, sp in speedups[:5]:
        f.write(f"- **{name}** ({src}): {sp:.1f}x ({t1/1000:.1f}s → {t128/1000:.1f}s)\n")

    f.write("\n**Observations:**\n")
    f.write("- Graphs with large dominant SCCs (e.g., social networks, random graphs) show ")
    f.write("the best parallel speedup as the global FW-BW phase parallelizes well.\n")
    f.write("- Web graphs (sk-2005, uk-2005, webbase-2001) show limited speedup due to their ")
    f.write("complex SCC structure with many small components requiring sequential FW-BW decomposition.\n")
    f.write("- Road networks and mesh graphs in directed form show poor parallel speedup ")
    f.write("because random direction assignment shatters the original large CC into millions of tiny SCCs.\n")

    f.write("\n### Undirected → Directed Impact on SCC Structure\n\n")
    f.write("Converting undirected graphs to directed by random edge orientation dramatically changes SCC structure:\n\n")
    # Compare bgr vs bgr_directed for matching graphs
    bgr_map = {r['graph']: r for r in bgr}
    f.write("| Graph | Undirected SCCs | Directed SCCs | SCC Increase | Largest SCC (undirected) | Largest SCC (directed) |\n")
    f.write("|-------|----------------:|--------------:|-------------:|-------------------------:|-----------------------:|\n")
    for r in sorted(bgr_dir, key=lambda x: -int(x['edges'])):
        if r['graph'] in bgr_map:
            u = bgr_map[r['graph']]
            u_sccs = int(u['numSCCs'])
            d_sccs = int(r['numSCCs'])
            ratio = f"{d_sccs/u_sccs:.0f}x" if u_sccs > 0 else "N/A"
            f.write(f"| {r['graph']} | {fmt(u_sccs)} | {fmt(d_sccs)} | {ratio} | {fmt(u['largestSCC_nodes'])} | {fmt(r['largestSCC_nodes'])} |\n")

    f.write("\n**Key findings:**\n")
    f.write("- Dense social/random graphs (com-Friendster, GAP-urand) retain a giant SCC even after random direction assignment.\n")
    f.write("- Sparse graphs (road networks, meshes, k-mer graphs) are completely shattered — ")
    f.write("millions of tiny SCCs replace the original single large component.\n")
    f.write("- This confirms that graph density is the key factor for SCC resilience under random orientation.\n")

    f.write("\n## 5. Methodology\n\n")
    f.write("- **SCC Algorithm:** par-scc Method 1 (Trim1 + Global FW-BW + Trim1 + FW-BW) for timing\n")
    f.write("- **Correctness:** Kosaraju's algorithm (sequential) for ground-truth SCC labels\n")
    f.write("- **WCC:** Parallel union-find with path compression and union by rank\n")
    f.write("- **Graph loading:** BGR format with 64-thread parallel I/O, converted to gm_graph CSR in memory\n")
    f.write("- **Timing:** Each thread configuration (1, 32, 64, 128) run independently; graph loaded once\n")

print(f"Report written: {OUT}")
