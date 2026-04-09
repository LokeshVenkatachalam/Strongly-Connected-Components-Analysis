#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOL="$SCRIPT_DIR/scc_analyzer"
CSV="$SCRIPT_DIR/results.csv"
LOG="$SCRIPT_DIR/run.log"

if [ ! -x "$TOOL" ]; then
    echo "ERROR: scc_analyzer not found. Run 'make' first."
    exit 1
fi

# CSV header
echo "graph,source,nodes,edges,numSCCs,numWCCs,largestSCC_nodes,largestSCC_edges,histogram,load_ms,scc_1t_ms,scc_32t_ms,scc_64t_ms,scc_128t_ms" > "$CSV"

process_dir() {
    local dir="$1"
    local label="$2"
    echo "=== Processing $label: $dir ==="
    for bgr_file in "$dir"/*.bgr; do
        [ -f "$bgr_file" ] || continue
        local name
        name=$(basename "$bgr_file" .bgr)
        echo "Processing: $name ($label)"
        "$TOOL" "$bgr_file" --csv "$CSV" 2>>"$LOG" || echo "  FAILED: $name"
        echo ""
    done
}

> "$LOG"

process_dir "/ssd/Graphs/bgr" "bgr"
process_dir "/ssd/Graphs/bgr_directed" "bgr_directed"

echo "=== Done ==="
echo "CSV: $CSV"
echo "Log: $LOG"
echo ""
echo "Graphs processed: $(tail -n +2 "$CSV" | wc -l)"
echo "Skipped: $(grep -c SKIPPED "$CSV" || true)"
