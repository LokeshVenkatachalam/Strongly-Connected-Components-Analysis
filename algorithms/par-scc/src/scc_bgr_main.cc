#include "gm.h"
#include "my_work_queue.h"
#include "scc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <omp.h>

node_t* G_SCC;
int32_t G_num_nodes;

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

template <class T>
T loadUnaligned(const uint8_t* address) {
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

uint64_t checkedAdd(uint64_t left, uint64_t right) {
    if (right > std::numeric_limits<uint64_t>::max() - left)
        throw std::overflow_error("BGR size addition overflow");
    return left + right;
}

uint64_t checkedMultiply(uint64_t left, uint64_t right) {
    if (right && left > std::numeric_limits<uint64_t>::max() / right)
        throw std::overflow_error("BGR size multiplication overflow");
    return left * right;
}

struct Mapping {
    int fd = -1;
    const uint8_t* data = nullptr;
    uint64_t bytes = 0;

    explicit Mapping(const std::string& path) {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("cannot open BGR input");
        struct stat info {};
        if (::fstat(fd, &info) != 0 || info.st_size < 9)
            throw std::runtime_error("truncated BGR input");
        bytes = static_cast<uint64_t>(info.st_size);
        void* mapped =
            ::mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED)
            throw std::runtime_error("cannot mmap BGR input");
        data = static_cast<const uint8_t*>(mapped);
        ::madvise(const_cast<uint8_t*>(data), bytes, MADV_SEQUENTIAL);
    }

    ~Mapping() {
        if (data) ::munmap(const_cast<uint8_t*>(data), bytes);
        if (fd >= 0) ::close(fd);
    }
};

struct BgrView {
    Mapping mapping;
    uint64_t n = 0;
    uint64_t m = 0;
    unsigned node_bytes = 0;
    unsigned edge_bytes = 0;
    uint64_t row_offset = 0;
    uint64_t column_offset = 0;

    explicit BgrView(const std::string& path) : mapping(path) {
        const uint8_t flags = mapping.data[0];
        if (flags & ~0x0bU)
            throw std::runtime_error("reserved BGR flags");
        node_bytes = (flags & 1U) ? 8 : 4;
        edge_bytes = (flags & 2U) ? 8 : 4;
        if (node_bytes != sizeof(node_t) || sizeof(edge_t) != 8)
            throw std::runtime_error(
                "par-scc BGR runner requires uint32 nodes and uint64 edges");
        row_offset = 1 + node_bytes + edge_bytes;
        n = node_bytes == 8
                ? loadUnaligned<uint64_t>(mapping.data + 1)
                : loadUnaligned<uint32_t>(mapping.data + 1);
        m = edge_bytes == 8
                ? loadUnaligned<uint64_t>(
                      mapping.data + 1 + node_bytes)
                : loadUnaligned<uint32_t>(
                      mapping.data + 1 + node_bytes);
        column_offset =
            checkedAdd(row_offset, checkedMultiply(n, edge_bytes));
        const uint64_t expected = checkedAdd(
            column_offset,
            checkedMultiply(m, node_bytes + ((flags & 8U) ? 4 : 0)));
        if (expected != mapping.bytes)
            throw std::runtime_error("BGR size mismatch");
        if (n > static_cast<uint64_t>(
                    std::numeric_limits<node_t>::max()))
            throw std::runtime_error("node count exceeds node_t");
    }

    edge_t rowEnd(uint64_t vertex) const {
        const uint8_t* value =
            mapping.data + row_offset + vertex * edge_bytes;
        return edge_bytes == 8
                   ? loadUnaligned<uint64_t>(value)
                   : loadUnaligned<uint32_t>(value);
    }

    node_t destination(uint64_t edge) const {
        const uint8_t* value =
            mapping.data + column_offset + edge * node_bytes;
        const uint64_t destination =
            node_bytes == 8
                ? loadUnaligned<uint64_t>(value)
                : loadUnaligned<uint32_t>(value);
        if (destination >= n)
            throw std::runtime_error("BGR destination out of range");
        return static_cast<node_t>(destination);
    }
};

void loadForwardBgr(gm_graph& graph, const std::string& path) {
    BgrView input(path);
    graph.prepare_external_creation(
        static_cast<node_t>(input.n), static_cast<edge_t>(input.m));

    graph.begin[0] = 0;
#pragma omp parallel for schedule(static)
    for (uint64_t vertex = 0; vertex < input.n; ++vertex) {
        graph.begin[vertex + 1] = input.rowEnd(vertex);
    }
    if (graph.begin[input.n] != input.m)
        throw std::runtime_error("invalid final BGR row end");

    std::atomic<bool> invalid{false};
#pragma omp parallel for schedule(static)
    for (uint64_t edge = 0; edge < input.m; ++edge) {
        try {
            graph.node_idx[edge] = input.destination(edge);
        } catch (...) {
            invalid.store(true, std::memory_order_relaxed);
        }
    }
    if (invalid.load(std::memory_order_relaxed))
        throw std::runtime_error("invalid BGR destination");
}

void loadReverseBgr(gm_graph& graph, const std::string& path) {
    BgrView input(path);
    if (input.n != static_cast<uint64_t>(graph.num_nodes()) ||
        input.m != static_cast<uint64_t>(graph.num_edges()))
        throw std::runtime_error("reverse BGR dimensions differ");
    graph.prepare_external_reverse_scc();

    graph.r_begin[0] = 0;
#pragma omp parallel for schedule(static)
    for (uint64_t vertex = 0; vertex < input.n; ++vertex) {
        graph.r_begin[vertex + 1] = input.rowEnd(vertex);
    }
    if (graph.r_begin[input.n] != input.m)
        throw std::runtime_error("invalid final reverse BGR row end");

    std::atomic<bool> invalid{false};
#pragma omp parallel for schedule(static)
    for (uint64_t edge = 0; edge < input.m; ++edge) {
        try {
            graph.r_node_idx[edge] = input.destination(edge);
        } catch (...) {
            invalid.store(true, std::memory_order_relaxed);
        }
    }
    if (invalid.load(std::memory_order_relaxed))
        throw std::runtime_error("invalid reverse BGR destination");
}

void initializeScc(gm_graph& graph, int threads) {
    gm_rt_initialize();
    gm_rt_set_num_threads(threads);
    omp_set_num_threads(threads);
    G_num_nodes = graph.num_nodes();
    G_SCC = new node_t[G_num_nodes];
#pragma omp parallel for schedule(static)
    for (int32_t i = 0; i < G_num_nodes; ++i)
        G_SCC[i] = gm_graph::NIL_NODE;

    work_q_init(threads);
    initialize_color();
    initialize_trim1();
    initialize_trim2();
    initialize_tarjan();
    initialize_analyze();
    initialize_global_fb();
}

void finalizeScc() {
    finalize_color();
    finalize_tarjan();
    finalize_trim2();
    finalize_analyze();
    delete[] G_SCC;
    G_SCC = nullptr;
}

void runMethod1(gm_graph& graph) {
    int trimmed = repeat_global_trim1(graph);
    const int current_color = get_curr_color();
    int current_count = G_num_nodes - trimmed;
    if (current_count == 0) return;

    (void)do_fw_bw_global_main(
        graph, current_color, current_count, false);
    (void)repeat_global_trim1_compact(graph);
    create_works_after_bfs_trim(graph);
    start_workers_fw_bw_dfs(graph, 1);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc != 3 && argc != 4) {
        std::fprintf(
            stderr,
            "usage: %s INPUT.bgr [REVERSE.bgr] THREADS\n",
            argv[0]);
        return 2;
    }

    const std::string input = argv[1];
    const std::string reverse = argc == 4 ? argv[2] : "";
    const int threads = std::stoi(argv[argc - 1]);
    if (threads <= 0 || threads > 256) {
        std::fprintf(stderr, "thread count must be in 1..256\n");
        return 2;
    }

    try {
        const auto total_start = Clock::now();
        gm_graph graph;

        const auto load_start = Clock::now();
        loadForwardBgr(graph, input);
        const double load_seconds = secondsSince(load_start);

        const auto transpose_start = Clock::now();
        if (reverse.empty())
            graph.make_reverse_edges_scc();
        else
            loadReverseBgr(graph, reverse);
        const double transpose_seconds = secondsSince(transpose_start);

        initializeScc(graph, threads);
        const auto scc_start = Clock::now();
        runMethod1(graph);
        const double scc_seconds = secondsSince(scc_start);

        uint64_t components = 0;
        for (node_t vertex = 0; vertex < graph.num_nodes(); ++vertex) {
            if (G_SCC[vertex] == vertex) ++components;
        }
        finalizeScc();

        std::cout << std::fixed << std::setprecision(9)
                  << "PAR_SCC_RESULT {"
                  << "\"input\":\"" << input << "\","
                  << "\"reverse_input\":\"" << reverse << "\","
                  << "\"vertices\":" << graph.num_nodes() << ","
                  << "\"edges\":" << graph.num_edges() << ","
                  << "\"threads\":" << threads << ","
                  << "\"method\":1,"
                  << "\"load_seconds\":" << load_seconds << ","
                  << "\"transpose_seconds\":" << transpose_seconds << ","
                  << "\"scc_seconds\":" << scc_seconds << ","
                  << "\"total_seconds\":" << secondsSince(total_start) << ","
                  << "\"components\":" << components
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "PAR_SCC_ERROR %s\n", error.what());
        return 1;
    }
}
