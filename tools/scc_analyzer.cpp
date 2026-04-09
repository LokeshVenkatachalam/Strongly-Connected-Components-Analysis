/*
 * scc_analyzer.cpp
 *
 * Loads BGR graph → gm_graph CSR → runs par-scc Method 2 at multiple thread counts
 * for timing, and uses Kosaraju for correct SCC labels. Also computes WCC.
 *
 * Usage: ./scc_analyzer <input.bgr> [--csv <output.csv>]
 */

#include "gm.h"
#include "scc.h"
#include "my_work_queue.h"

#include "graph.h"
#include "bgr_format.h"
#include "bgr_reader.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <sys/time.h>
#include <omp.h>

// par-scc globals
node_t* G_SCC;
int32_t G_num_nodes;

static double getTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec * 0.001;
}

// ============================================================
// Parallel Union-Find for WCC
// ============================================================
static int64_t computeWCC(gm_graph& G) {
    node_t N = G.num_nodes();
    std::vector<int32_t> parent(N), rnk(N, 0);
    #pragma omp parallel for
    for (int32_t i = 0; i < N; i++) parent[i] = i;

    auto find = [&](int32_t x) -> int32_t {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    auto unite = [&](int32_t a, int32_t b) {
        while (true) {
            a = find(a); b = find(b);
            if (a == b) return;
            if (rnk[a] < rnk[b]) std::swap(a, b);
            if (__sync_bool_compare_and_swap(&parent[b], b, a)) {
                if (rnk[a] == rnk[b]) __sync_fetch_and_add(&rnk[a], 1);
                return;
            }
        }
    };

    #pragma omp parallel for schedule(dynamic, 4096)
    for (node_t u = 0; u < N; u++) {
        for (edge_t ei = G.begin[u]; ei < G.begin[u + 1]; ei++) {
            unite(u, G.node_idx[ei]);
        }
    }

    int64_t count = 0;
    #pragma omp parallel for reduction(+:count)
    for (int32_t i = 0; i < N; i++) {
        if (find(i) == i) count++;
    }
    return count;
}

// ============================================================
// Correct SCC via Kosaraju (sequential)
// ============================================================
static void kosaraju(gm_graph& G, node_t* scc_labels) {
    node_t N = G.num_nodes();
    edge_t M = G.num_edges();

    // Step 1: DFS to get finish order
    std::vector<node_t> finish_order;
    finish_order.reserve(N);
    std::vector<bool> visited(N, false);

    for (node_t start = 0; start < N; start++) {
        if (visited[start]) continue;
        std::vector<std::pair<node_t, edge_t>> stk;
        stk.push_back({start, G.begin[start]});
        visited[start] = true;
        while (!stk.empty()) {
            auto& [u, ei] = stk.back();
            if (ei < G.begin[u + 1]) {
                node_t v = G.node_idx[ei];
                ei++;
                if (!visited[v]) {
                    visited[v] = true;
                    stk.push_back({v, G.begin[v]});
                }
            } else {
                finish_order.push_back(u);
                stk.pop_back();
            }
        }
    }

    // Step 2: Build reverse adj using gm_graph's reverse edges if available,
    // otherwise build manually
    std::vector<edge_t> r_begin_vec;
    std::vector<node_t> r_node_idx_vec;
    edge_t* rb = nullptr;
    node_t* rn = nullptr;

    if (G.has_reverse_edge()) {
        rb = G.r_begin;
        rn = G.r_node_idx;
    } else {
        // Build reverse manually
        r_begin_vec.resize(N + 1, 0);
        // Count in-degrees
        for (edge_t i = 0; i < M; i++) {
            r_begin_vec[G.node_idx[i] + 1]++;
        }
        for (node_t i = 1; i <= N; i++) {
            r_begin_vec[i] += r_begin_vec[i - 1];
        }
        r_node_idx_vec.resize(M);
        std::vector<edge_t> r_pos(r_begin_vec.begin(), r_begin_vec.end());
        for (node_t u = 0; u < N; u++) {
            for (edge_t e = G.begin[u]; e < G.begin[u + 1]; e++) {
                node_t v = G.node_idx[e];
                r_node_idx_vec[r_pos[v]++] = u;
            }
        }
        rb = r_begin_vec.data();
        rn = r_node_idx_vec.data();
    }

    // Step 3: DFS on reverse graph in reverse finish order
    std::fill(visited.begin(), visited.end(), false);
    for (int fi = N - 1; fi >= 0; fi--) {
        node_t root = finish_order[fi];
        if (visited[root]) continue;
        std::vector<node_t> stk = {root};
        visited[root] = true;
        scc_labels[root] = root;
        while (!stk.empty()) {
            node_t u = stk.back(); stk.pop_back();
            for (edge_t e = rb[u]; e < rb[u + 1]; e++) {
                node_t v = rn[e];
                if (!visited[v]) {
                    visited[v] = true;
                    scc_labels[v] = root;
                    stk.push_back(v);
                }
            }
        }
    }
}

// ============================================================
// SCC Metrics
// ============================================================
struct SCCMetrics {
    int64_t numSCCs;
    int64_t largestSCC_nodes;
    int64_t largestSCC_edges;
    std::map<std::string, int64_t> histogram;
};

static std::string sizeBucket(int64_t sz) {
    if (sz == 0) return "0";
    if (sz <= 10) return "1-10";
    if (sz <= 100) return "11-100";
    if (sz <= 1000) return "101-1K";
    if (sz <= 10000) return "1K-10K";
    if (sz <= 100000) return "10K-100K";
    if (sz <= 1000000) return "100K-1M";
    if (sz <= 10000000) return "1M-10M";
    if (sz <= 100000000) return "10M-100M";
    if (sz <= 1000000000LL) return "100M-1B";
    return "1B+";
}

static SCCMetrics analyzeSCC(gm_graph& G, node_t* scc_labels) {
    SCCMetrics m = {};
    node_t N = G.num_nodes();

    // Count SCC sizes
    std::vector<int64_t> scc_size(N, 0);
    for (node_t i = 0; i < N; i++) {
        scc_size[scc_labels[i]]++;
    }

    int64_t numSCCs = 0, maxNodes = 0;
    node_t maxRoot = 0;
    for (node_t i = 0; i < N; i++) {
        if (scc_labels[i] == i) {
            numSCCs++;
            if (scc_size[i] > maxNodes) {
                maxNodes = scc_size[i];
                maxRoot = i;
            }
        }
    }

    // Count edges in largest SCC
    int64_t largestEdges = 0;
    #pragma omp parallel for reduction(+:largestEdges) schedule(dynamic, 4096)
    for (node_t u = 0; u < N; u++) {
        if (scc_labels[u] != maxRoot) continue;
        for (edge_t ei = G.begin[u]; ei < G.begin[u + 1]; ei++) {
            if (scc_labels[G.node_idx[ei]] == maxRoot) largestEdges++;
        }
    }

    // Histogram
    std::map<std::string, int64_t> hist;
    for (node_t i = 0; i < N; i++) {
        if (scc_labels[i] == i) {
            hist[sizeBucket(scc_size[i])]++;
        }
    }

    m.numSCCs = numSCCs;
    m.largestSCC_nodes = maxNodes;
    m.largestSCC_edges = largestEdges;
    m.histogram = hist;
    return m;
}

// ============================================================
// par-scc Method 2 runner (for timing only)
// ============================================================
static double runParSCCMethod2(gm_graph& G, int numThreads) {
    node_t N = G.num_nodes();

    gm_rt_set_num_threads(numThreads);
    omp_set_num_threads(numThreads);

    G_SCC = new node_t[N];
    #pragma omp parallel for
    for (int32_t i = 0; i < N; i++)
        G_SCC[i] = gm_graph::NIL_NODE;

    work_q_init(numThreads);
    initialize_color();
    initialize_trim1();
    initialize_trim2();
    initialize_tarjan();
    initialize_analyze();
    initialize_global_fb();

    double t0 = getTimeMs();

    int trimmed = repeat_global_trim1(G);
    int curr_color = get_curr_color();
    int curr_count = G_num_nodes - trimmed;

    if (curr_count > 0) {
        int scc_size = do_fw_bw_global_main(G, curr_color, curr_count, false);
        trimmed = repeat_global_trim1_compact(G);
        curr_count = curr_count - trimmed - scc_size;

        if (curr_count > 0) {
            create_works_after_bfs_trim(G);
            start_workers_fw_bw_dfs(G, 40);
        }
    }

    double elapsed = getTimeMs() - t0;

    // Count for verification
    int count = 0;
    for (int32_t i = 0; i < N; i++)
        if (G_SCC[i] == i) count++;
    std::cerr << "    par-scc SCCs=" << count << " time=" << elapsed << "ms\n";

    finalize_color();
    finalize_tarjan();
    finalize_trim2();
    finalize_analyze();
    delete[] G_SCC;
    G_SCC = nullptr;

    return elapsed;
}

// ============================================================
// Memory estimate
// ============================================================
static size_t estimateMemory(uint64_t N, uint64_t M) {
    // gm_graph: begin(N+1)*8 + node_idx(M)*4 + r_begin(N+1)*8 + r_node_idx(M)*4 
    //         + e_rev2idx(M)*8 + e_idx2idx(M)*8
    // SCC arrays: G_SCC(N)*4 + G_Color(N)*4 + various(N)*4
    // Kosaraju: finish_order(N)*4 + visited(N)*1 + scc_labels(N)*4
    return (N + 1) * 8 * 2   // begin + r_begin
         + M * 4 * 2         // node_idx + r_node_idx
         + M * 8 * 2         // e_rev2idx + e_idx2idx
         + N * 4 * 6;        // SCC arrays + Kosaraju
}

static std::string extractName(const std::string& path) {
    auto f = std::filesystem::path(path).filename().string();
    if (f.size() > 4 && f.substr(f.size() - 4) == ".bgr")
        return f.substr(0, f.size() - 4);
    return f;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.bgr> [--csv <file>]\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string csvFile;
    for (int i = 2; i < argc - 1; i++)
        if (std::string(argv[i]) == "--csv") csvFile = argv[i + 1];

    std::string graphName = extractName(inputFile);
    std::string sourceDir = (inputFile.find("bgr_directed") != std::string::npos) ? "bgr_directed" : "bgr";

    std::cerr << "=== SCC Analysis: " << graphName << " (" << sourceDir << ") ===\n";

    // Check memory
    {
        int fd = open(inputFile.c_str(), O_RDONLY);
        if (fd < 0) { std::cerr << "Cannot open\n"; return 1; }
        uint8_t buf[17]; pread(fd, buf, 17, 0); close(fd);
        BGRHeader h; decodeBGRMeta(buf, h);
        double memGB = estimateMemory(h.numNodes, h.numEdges) / (1024.0 * 1024 * 1024);
        std::cerr << "  N=" << h.numNodes << " M=" << h.numEdges << " est=" << memGB << "GB\n";
        if (memGB > 330.0) {
            std::cerr << "  SKIPPED: exceeds memory\n";
            std::cout << graphName << "\t" << sourceDir << "\t" << h.numNodes << "\t" << h.numEdges
                      << "\tSKIPPED\n";
            return 0;
        }
    }

    // Load BGR with 64 threads
    omp_set_num_threads(64);
    double t0 = getTimeMs();
    Graph bgr = readBGR(inputFile);
    double loadTime = getTimeMs() - t0;
    std::cerr << "  Load: " << loadTime << "ms\n";

    // Convert to gm_graph
    gm_graph G;
    node_t N = (node_t) bgr.numNodes;
    edge_t M = (edge_t) bgr.numEdges;
    G.prepare_external_creation(N, M);
    G.begin[0] = 0;
    #pragma omp parallel for
    for (node_t i = 0; i < N; i++) G.begin[i + 1] = (edge_t) bgr.rowPtr[i];
    #pragma omp parallel for
    for (edge_t i = 0; i < M; i++) G.node_idx[i] = (node_t) bgr.colIdx[i];
    bgr.clear();

    G_num_nodes = N;
    gm_rt_initialize();

    // Make reverse edges (needed by par-scc)
    std::cerr << "  Reverse edges...\n";
    omp_set_num_threads(64); gm_rt_set_num_threads(64);
    t0 = getTimeMs();
    G.make_reverse_edges();
    double revTime = getTimeMs() - t0;
    std::cerr << "  Reverse: " << revTime << "ms\n";

    // WCC
    std::cerr << "  WCC...\n";
    omp_set_num_threads(64);
    t0 = getTimeMs();
    int64_t numWCCs = computeWCC(G);
    double wccTime = getTimeMs() - t0;
    std::cerr << "  WCCs=" << numWCCs << " time=" << wccTime << "ms\n";

    // Correct SCC via Kosaraju
    std::cerr << "  Kosaraju SCC...\n";
    std::vector<node_t> scc_labels(N, -1);
    t0 = getTimeMs();
    kosaraju(G, scc_labels.data());
    double kosarajuTime = getTimeMs() - t0;
    std::cerr << "  Kosaraju: " << kosarajuTime << "ms\n";

    SCCMetrics metrics = analyzeSCC(G, scc_labels.data());
    std::cerr << "  SCCs=" << metrics.numSCCs
              << " largestN=" << metrics.largestSCC_nodes
              << " largestE=" << metrics.largestSCC_edges << "\n";
    scc_labels.clear(); scc_labels.shrink_to_fit();

    // par-scc timing at multiple thread counts
    int threadCounts[] = {1, 32, 64, 128};
    double sccTimes[4] = {};
    for (int ti = 0; ti < 4; ti++) {
        std::cerr << "  par-scc " << threadCounts[ti] << "t...\n";
        sccTimes[ti] = runParSCCMethod2(G, threadCounts[ti]);
    }

    // Build histogram string
    std::string histStr;
    const char* buckets[] = {"0", "1-10", "11-100", "101-1K", "1K-10K",
        "10K-100K", "100K-1M", "1M-10M", "10M-100M", "100M-1B", "1B+"};
    for (const char* b : buckets) {
        auto it = metrics.histogram.find(b);
        if (it != metrics.histogram.end()) {
            if (!histStr.empty()) histStr += "; ";
            histStr += std::string(b) + ":" + std::to_string(it->second);
        }
    }

    // Output
    std::cout << graphName << "\t" << sourceDir << "\t"
              << N << "\t" << M << "\t"
              << metrics.numSCCs << "\t" << numWCCs << "\t"
              << metrics.largestSCC_nodes << "\t" << metrics.largestSCC_edges << "\t"
              << histStr << "\t"
              << std::fixed << std::setprecision(1)
              << loadTime << "\t"
              << sccTimes[0] << "\t" << sccTimes[1] << "\t"
              << sccTimes[2] << "\t" << sccTimes[3] << "\n";

    if (!csvFile.empty()) {
        std::ofstream csv(csvFile, std::ios::app);
        csv << graphName << "," << sourceDir << ","
            << N << "," << M << ","
            << metrics.numSCCs << "," << numWCCs << ","
            << metrics.largestSCC_nodes << "," << metrics.largestSCC_edges << ","
            << "\"" << histStr << "\","
            << std::fixed << std::setprecision(1)
            << loadTime << ","
            << sccTimes[0] << "," << sccTimes[1] << ","
            << sccTimes[2] << "," << sccTimes[3] << "\n";
    }

    std::cerr << "=== Done: " << graphName << " ===\n\n";
    return 0;
}
