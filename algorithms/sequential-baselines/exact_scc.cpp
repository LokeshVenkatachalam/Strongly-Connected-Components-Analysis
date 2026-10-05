// Linux, C++17. BGR layout follows pagerank-hybrid-active/src/bgr_loader.hxx
// at 9a2a0729250407df8241ac2e7798c80381bad220; no CUDA dependency or sanitation.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using U64 = uint64_t;
constexpr U64 NONE = std::numeric_limits<U64>::max();
using Clock = std::chrono::steady_clock;
static_assert(sizeof(size_t) == 8 && sizeof(off_t) == 8, "64-bit platform required");

U64 add(U64 a, U64 b) {
    if (b > NONE - a) throw std::runtime_error("64-bit addition overflow");
    return a + b;
}
U64 mul(U64 a, U64 b) {
    if (a && b > NONE / a) throw std::runtime_error("64-bit multiplication overflow");
    return a * b;
}
double seconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}
std::string quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c >= 32) out += c;
        else {
            const char* hex = "0123456789abcdef";
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        }
    }
    return out + '"';
}
template<class T> U64 load(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T)); // BGR arrays start at unaligned byte offsets.
    return v;
}

struct Graph {
    int fd = -1;
    const uint8_t* data = nullptr;
    U64 n = 0, m = 0, file_bytes = 0, row_offset = 0, col_offset = 0, mapped_bytes = 0;
    unsigned flags = 0, node_bytes = 0, edge_bytes = 0;
    struct stat before {};

    explicit Graph(const char* path) {
        const uint32_t endian = 1;
        if (*reinterpret_cast<const uint8_t*>(&endian) != 1)
            throw std::runtime_error("little-endian host required");
        fd = open(path, O_RDONLY);
        if (fd < 0) throw std::runtime_error("open: " + std::string(std::strerror(errno)));
        try {
            if (fstat(fd, &before)) throw std::runtime_error("fstat failed");
            if (before.st_size < 9) throw std::runtime_error("truncated BGR header");
            file_bytes = static_cast<U64>(before.st_size);
            uint8_t header[17] {};
            const ssize_t got = pread(fd, header, sizeof(header), 0);
            if (got < 9) throw std::runtime_error("BGR header read failed");
            flags = header[0];
            if (flags & ~11U) throw std::runtime_error("unsupported BGR flags");
            node_bytes = (flags & 1U) ? 8 : 4;
            edge_bytes = (flags & 2U) ? 8 : 4;
            row_offset = 1 + node_bytes + edge_bytes;
            if (static_cast<U64>(got) < row_offset) throw std::runtime_error("truncated BGR header");
            n = node_bytes == 8 ? load<U64>(header + 1) : load<uint32_t>(header + 1);
            m = edge_bytes == 8 ? load<U64>(header + 1 + node_bytes)
                               : load<uint32_t>(header + 1 + node_bytes);
            if (n == NONE) throw std::runtime_error("vertex count collides with sentinel");
            col_offset = add(row_offset, mul(n, edge_bytes));
            mapped_bytes = add(col_offset, mul(m, node_bytes));
            const U64 expected = add(mapped_bytes, (flags & 8U) ? mul(m, 4) : 0);
            if (file_bytes != expected) throw std::runtime_error("BGR file-size mismatch");
            void* p = mmap(nullptr, mapped_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED) throw std::runtime_error("read-only graph mmap failed");
            data = static_cast<const uint8_t*>(p);
        } catch (...) { close(fd); fd = -1; throw; }
    }
    ~Graph() {
        if (data) munmap(const_cast<uint8_t*>(data), mapped_bytes);
        if (fd >= 0) close(fd);
    }
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    U64 end(U64 u) const {
        const auto* p = data + row_offset + u * edge_bytes;
        return edge_bytes == 8 ? load<U64>(p) : load<uint32_t>(p);
    }
    U64 begin(U64 u) const { return u ? end(u - 1) : 0; }
    U64 destination(U64 e) const {
        const auto* p = data + col_offset + e * node_bytes;
        return node_bytes == 8 ? load<U64>(p) : load<uint32_t>(p);
    }
    void advice(int value) const {
        if (madvise(const_cast<uint8_t*>(data), mapped_bytes, value))
            throw std::runtime_error("madvise failed: " + std::string(std::strerror(errno)));
    }
    void unchanged() const {
        struct stat after {};
        if (fstat(fd, &after) || after.st_size != before.st_size ||
            after.st_mtim.tv_sec != before.st_mtim.tv_sec ||
            after.st_mtim.tv_nsec != before.st_mtim.tv_nsec ||
            after.st_ctim.tv_sec != before.st_ctim.tv_sec ||
            after.st_ctim.tv_nsec != before.st_ctim.tv_nsec)
            throw std::runtime_error("input changed during analysis");
    }
};

struct Frame { U64 vertex, next_edge; };
static_assert(sizeof(Frame) == 16, "preflight assumes 16-byte DFS frames");
struct Result {
    U64 components = 0, largest = 0, minimum = NONE, largest_id = NONE, ties = 0;
    U64 internal = 0, external = 0, incoming = 0, max_dfs = 0, max_active = 0;
    U64 visited_edges = 0, accounted_vertices = 0, cross_edges = 0;
    double scc_seconds = 0, counting_seconds = 0, forward_seconds = 0;
    U64 forward_reached = 0;
};

Result analyze(const Graph& g) {
    Result r;
    // Fixed capacities avoid vector growth peaks. Only discovery/component arrays
    // are initialized; the DFS/active stacks touch just their actual depths.
    mul(g.n, 48);
    auto discovery = std::make_unique<U64[]>(g.n);
    std::unique_ptr<U64[]> low(new U64[g.n]), component(new U64[g.n]), active(new U64[g.n]);
    std::unique_ptr<Frame[]> dfs(new Frame[g.n]);
    std::fill_n(component.get(), g.n, NONE);
    U64 timer = 0, depth = 0, active_size = 0;
    auto discover = [&](U64 v) {
        discovery[v] = low[v] = ++timer;
        active[active_size++] = v;
        dfs[depth++] = {v, g.begin(v)};
        r.max_dfs = std::max(r.max_dfs, depth);
        r.max_active = std::max(r.max_active, active_size);
    };
    auto start = Clock::now();
    g.advice(MADV_RANDOM);
    std::cerr << "phase=tarjan\n";
    for (U64 root = 0; root < g.n; ++root) {
        if (discovery[root]) continue;
        discover(root);
        while (depth) {
            Frame& f = dfs[depth - 1];
            const U64 u = f.vertex;
            if (f.next_edge < g.end(u)) {
                const U64 v = g.destination(f.next_edge++);
                ++r.visited_edges;
                if ((r.visited_edges & ((1ULL << 28) - 1)) == 0)
                    std::cerr << "phase=tarjan edges=" << r.visited_edges << "/" << g.m
                              << " discovered=" << timer << "/" << g.n
                              << " assigned=" << r.accounted_vertices << " components=" << r.components
                              << " seconds=" << seconds(start) << '\n';
                if (!discovery[v]) discover(v);
                else if (component[v] == NONE) low[u] = std::min(low[u], discovery[v]);
                continue;
            }
            if (low[u] == discovery[u]) {
                U64 count = 0, minimum = NONE, v;
                do {
                    if (!active_size) throw std::runtime_error("Tarjan active-stack underflow");
                    v = active[--active_size];
                    component[v] = r.components;
                    ++count;
                    minimum = std::min(minimum, v);
                } while (v != u);
                r.accounted_vertices += count;
                if (count > r.largest) {
                    r.largest = count; r.minimum = minimum; r.largest_id = r.components; r.ties = 1;
                } else if (count == r.largest) {
                    ++r.ties;
                    if (minimum < r.minimum) { r.minimum = minimum; r.largest_id = r.components; }
                }
                ++r.components;
            }
            --depth;
            if (depth) {
                const U64 parent = dfs[depth - 1].vertex;
                low[parent] = std::min(low[parent], low[u]);
            }
        }
    }
    if (timer != g.n || active_size || r.visited_edges != g.m || r.accounted_vertices != g.n)
        throw std::runtime_error("Tarjan coverage invariant failed");
    r.scc_seconds = seconds(start);

    // Independent outgoing reachability from the chosen component's minimum ID.
    // It proves forward reachability, NOT reverse reachability on its own.
    std::cerr << "phase=forward-verification\n";
    start = Clock::now();
    std::fill_n(discovery.get(), g.n, 0);
    U64 size = 0;
    U64 forward_edges = 0;
    if (g.n) { active[size++] = r.minimum; discovery[r.minimum] = 1; }
    while (size) {
        const U64 u = active[--size];
        ++r.forward_reached;
        for (U64 e = g.begin(u); e < g.end(u); ++e) {
            if ((++forward_edges & ((1ULL << 28) - 1)) == 0)
                std::cerr << "phase=forward-verification edges=" << forward_edges
                          << " reached=" << r.forward_reached << "/" << r.largest
                          << " seconds=" << seconds(start) << '\n';
            const U64 v = g.destination(e);
            if (component[v] == r.largest_id && !discovery[v]) {
                discovery[v] = 1; active[size++] = v;
            }
        }
    }
    if (r.forward_reached != r.largest) throw std::runtime_error("largest SCC forward check failed");
    r.forward_seconds = seconds(start);
    discovery.reset(); low.reset(); active.reset(); dfs.reset();

    std::cerr << "phase=counting-and-condensation-verification\n";
    start = Clock::now();
    g.advice(MADV_SEQUENTIAL);
    U64 selected = 0, selected_min = NONE;
    for (U64 u = 0; u < g.n; ++u) {
        const U64 cu = component[u];
        if (cu == NONE || cu >= r.components) throw std::runtime_error("unassigned component");
        const bool inside = cu == r.largest_id;
        if (inside) { ++selected; selected_min = std::min(selected_min, u); }
        bool has_incoming = false;
        for (U64 e = g.begin(u); e < g.end(u); ++e) {
            const U64 cv = component[g.destination(e)];
            // Tarjan emits sink components first; every cross arc must decrease ID.
            if (cu != cv) {
                if (cu <= cv) throw std::runtime_error("condensation order invariant failed");
                ++r.cross_edges;
            }
            if (cv == r.largest_id) {
                if (inside) ++r.internal;
                else { ++r.external; has_incoming = true; }
            }
        }
        if (has_incoming) ++r.incoming;
    }
    if (selected != r.largest || selected_min != r.minimum ||
        r.incoming > g.n - r.largest || add(r.internal, r.external) > g.m)
        throw std::runtime_error("counting invariant failed");
    r.counting_seconds = seconds(start);
    return r;
}

void arithmetic_test() {
    if (mul(1ULL << 27, 48) != 6442450944ULL ||
        add((1ULL << 32), 17) != 4294967313ULL ||
        mul(91792261600ULL, 4) != 367169046400ULL)
        throw std::runtime_error("wide arithmetic test failed");
    bool a = false, b = false;
    try { (void)add(NONE, 1); } catch (const std::runtime_error&) { a = true; }
    try { (void)mul(NONE, 8); } catch (const std::runtime_error&) { b = true; }
    if (!a || !b) throw std::runtime_error("overflow detection test failed");
    const uint8_t raw[] = {0, 17, 0, 0, 0, 1, 0, 0, 0};
    if (load<U64>(raw + 1) != 4294967313ULL)
        throw std::runtime_error("unaligned 64-bit offset test failed");
}

int main(int argc, char** argv) {
    const auto total_start = Clock::now();
    try {
        arithmetic_test();
        if (argc == 2 && std::string(argv[1]) == "--arithmetic-test") {
            std::cout << "{\"status\":\"passed\",\"index_bits\":64}\n"; return 0;
        }
        const bool validate_only = argc == 3 && std::string(argv[1]) == "--validate-only";
        if (argc != 2 && !validate_only)
            throw std::runtime_error("usage: exact_scc [--validate-only] INPUT.bgr | --arithmetic-test");
        Graph g(argv[validate_only ? 2 : 1]);
        std::cerr << "phase=input-validation n=" << g.n << " m=" << g.m << '\n';
        const auto validation_start = Clock::now();
        g.advice(MADV_SEQUENTIAL);
        U64 previous = 0;
        for (U64 u = 0; u < g.n; ++u) {
            const U64 end = g.end(u);
            if (end < previous || end > g.m) throw std::runtime_error("invalid cumulative row end");
            previous = end;
        }
        if (previous != g.m) throw std::runtime_error("final row end differs from header M");
        U64 invalid = 0;
        std::vector<std::pair<U64, U64>> examples;
        for (U64 e = 0; e < g.m; ++e) {
            const U64 v = g.destination(e);
            if (v >= g.n) {
                ++invalid;
                if (examples.size() < 8) examples.emplace_back(e, v);
            }
        }
        g.unchanged();
        const double validation_seconds = seconds(validation_start);
        Result r;
        if (!invalid && !validate_only) r = analyze(g);
        g.unchanged();
        struct rusage usage {};
        if (getrusage(RUSAGE_SELF, &usage)) throw std::runtime_error("getrusage failed");
        std::cout << std::setprecision(15)
            << "{\"status\":" << quote(invalid ? "invalid_endpoints" : (validate_only ? "validated" : "complete"))
            << ",\"exact\":" << (invalid || validate_only ? "false" : "true")
            << ",\"N\":" << g.n << ",\"M\":" << g.m << ",\"flags\":" << g.flags
            << ",\"invalid_endpoints\":" << invalid << ",\"valid_edge_entries\":" << g.m - invalid
            << ",\"row_offset\":" << g.row_offset << ",\"col_offset\":" << g.col_offset
            << ",\"mapped_bytes\":" << g.mapped_bytes << ",\"array_capacity_bytes\":" << mul(g.n, 48)
            << ",\"validation_seconds\":" << validation_seconds
            << ",\"runtime_seconds\":" << seconds(total_start)
            << ",\"peak_rss_bytes\":" << mul(static_cast<U64>(usage.ru_maxrss), 1024)
            << ",\"major_faults\":" << usage.ru_majflt << ",\"minor_faults\":" << usage.ru_minflt
            << ",\"invalid_examples\":[";
        for (size_t i = 0; i < examples.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << "{\"edge_index\":" << examples[i].first
                      << ",\"destination\":" << examples[i].second << '}';
        }
        std::cout << ']';
        if (!invalid && !validate_only) {
            std::cout << ",\"scc_count\":" << r.components
                << ",\"largest_scc_vertices\":" << r.largest
                << ",\"largest_scc_min_vertex\":" << (g.n ? std::to_string(r.minimum) : "null")
                << ",\"largest_size_tie_count\":" << r.ties
                << ",\"direct_incoming_vertices\":" << r.incoming
                << ",\"largest_scc_internal_edges\":" << r.internal
                << ",\"external_incoming_edges\":" << r.external
                << ",\"all_edges_into_scc\":" << add(r.internal, r.external)
                << ",\"max_dfs_depth\":" << r.max_dfs << ",\"max_active_stack\":" << r.max_active
                << ",\"tarjan_edges_visited\":" << r.visited_edges
                << ",\"accounted_vertices\":" << r.accounted_vertices
                << ",\"cross_component_edges_checked\":" << r.cross_edges
                << ",\"forward_reached\":" << r.forward_reached
                << ",\"scc_seconds\":" << r.scc_seconds
                << ",\"counting_seconds\":" << r.counting_seconds
                << ",\"forward_verification_seconds\":" << r.forward_seconds;
        }
        std::cout << "}\n";
        return invalid ? 2 : 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        std::cout << "{\"status\":\"error\",\"exact\":false,\"error\":" << quote(e.what())
                  << ",\"runtime_seconds\":" << seconds(total_start) << "}\n";
        return 1;
    }
}
