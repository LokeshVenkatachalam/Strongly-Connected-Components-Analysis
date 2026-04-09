#!/bin/bash
set -euo pipefail

CONVERTER="/hdd/lokeshvenkatachalam/scc/bgr2scc"
WANG="/hdd/lokeshvenkatachalam/Parallel-Strong-Connectivity/src/scc"
GBBS="/hdd/lokeshvenkatachalam/Parallel-Strong-Connectivity/baselines/gbbs/benchmarks/StronglyConnectedComponents/RandomGreedyBGSS16/StronglyConnectedComponents"
ISPAN="/hdd/lokeshvenkatachalam/iSpan/src/ispan"
CSV="/hdd/lokeshvenkatachalam/scc/modern_scc_results.csv"

THREADS="1 32 64 128"

echo "graph,source,nodes,edges,load_ms,algorithm,threads,scc_ms,num_sccs" > "$CSV"

cleanup_shm() {
    rm -f /dev/shm/scc_wang.bin /dev/shm/scc_ispan_fw_beg.bin \
          /dev/shm/scc_ispan_fw_csr.bin /dev/shm/scc_ispan_bw_beg.bin \
          /dev/shm/scc_ispan_bw_csr.bin
}
trap cleanup_shm EXIT

process_graph() {
    local bgr="$1" source="$2"
    local name=$(basename "$bgr" .bgr)
    echo "======== $name ($source) ========"

    cleanup_shm

    # Convert BGR → /dev/shm formats
    local info
    info=$("$CONVERTER" "$bgr" 2>/dev/null) || { echo "  SKIP: converter failed"; return; }
    local N=$(echo "$info" | cut -f1)
    local M=$(echo "$info" | cut -f2)
    local load_ms=$(echo "$info" | cut -f3)
    local ispan_ok=$(echo "$info" | cut -f5)
    echo "  N=$N M=$M load=${load_ms}ms ispan=$ispan_ok"

    # Wang et al.
    for t in $THREADS; do
        local out
        out=$(PARLAY_NUM_THREADS=$t timeout 600 "$WANG" /dev/shm/scc_wang.bin \
              -t 1 -status -local_reach -local_scc 2>&1) || out="FAIL"
        local scc_ms=$(echo "$out" | grep "scc cost:" | tail -1 | awk '{printf "%.1f", $3*1000}')
        local nsccs=$(echo "$out" | grep "n_scc" | awk '{print $3}')
        [ -z "$scc_ms" ] && scc_ms="FAIL"
        [ -z "$nsccs" ] && nsccs=""
        echo "  wang ${t}t: ${scc_ms}ms sccs=$nsccs"
        echo "$name,$source,$N,$M,$load_ms,wang,$t,$scc_ms,$nsccs" >> "$CSV"
    done

    # GBBS
    for t in $THREADS; do
        local out
        out=$(PARLAY_NUM_THREADS=$t timeout 600 "$GBBS" -rounds 1 -stats -b \
              /dev/shm/scc_wang.bin 2>&1) || out="FAIL"
        local time_s=$(echo "$out" | grep "Running Time:" | awk '{print $4}')
        local nsccs=$(echo "$out" | grep "n_scc" | awk '{print $3}')
        local scc_ms=""
        [ -n "$time_s" ] && scc_ms=$(echo "$time_s" | awk '{printf "%.1f", $1*1000}')
        [ -z "$scc_ms" ] && scc_ms="FAIL"
        [ -z "$nsccs" ] && nsccs=""
        echo "  gbbs ${t}t: ${scc_ms}ms sccs=$nsccs"
        echo "$name,$source,$N,$M,$load_ms,gbbs,$t,$scc_ms,$nsccs" >> "$CSV"
    done

    # iSpan (only if int32 fits)
    if [ "$ispan_ok" = "1" ]; then
        for t in $THREADS; do
            local out
            out=$(timeout 600 "$ISPAN" \
                /dev/shm/scc_ispan_fw_beg.bin /dev/shm/scc_ispan_fw_csr.bin \
                /dev/shm/scc_ispan_bw_beg.bin /dev/shm/scc_ispan_bw_csr.bin \
                "$t" 1 100 4 0.1 1 2>&1) || out="FAIL"
            # iSpan outputs "Total time, X.XXX" (in ms) and "total, XXXX" for SCC count
            local scc_ms=$(echo "$out" | grep "^Total time," | awk -F, '{printf "%.1f", $2}')
            local nsccs=$(echo "$out" | grep "^total," | awk -F, '{printf "%d", $2}')
            [ -z "$scc_ms" ] && scc_ms="FAIL"
            [ -z "$nsccs" ] && nsccs=""
            echo "  ispan ${t}t: ${scc_ms}ms sccs=$nsccs"
            echo "$name,$source,$N,$M,$load_ms,ispan,$t,$scc_ms,$nsccs" >> "$CSV"
        done
    else
        for t in $THREADS; do
            echo "  ispan ${t}t: SKIP"
            echo "$name,$source,$N,$M,$load_ms,ispan,$t,SKIP," >> "$CSV"
        done
    fi

    cleanup_shm
    echo ""
}

# Web graphs first
for g in GD96_a webbase-1M_connected indochina-2004 arabic-2005 uk-2005 sk-2005 it-2004 twitter7 webbase-2001; do
    f="/ssd/Graphs/bgr/${g}.bgr"
    [ -f "$f" ] && process_graph "$f" "bgr"
done

# Other bgr graphs
for f in /ssd/Graphs/bgr/*.bgr; do
    name=$(basename "$f" .bgr)
    case "$name" in GD96_a|webbase-1M_connected|indochina-2004|arabic-2005|uk-2005|sk-2005|it-2004|twitter7|webbase-2001) continue;; esac
    case "$name" in clueweb12|eu-2015|gsh-2015|uk-2014|AGATHA_2015) echo "SKIP: $name (too large)"; continue;; esac
    process_graph "$f" "bgr"
done

# bgr_directed graphs
for f in /ssd/Graphs/bgr_directed/*.bgr; do
    process_graph "$f" "bgr_directed"
done

echo "=== DONE ==="
echo "Results: $CSV"
wc -l "$CSV"
