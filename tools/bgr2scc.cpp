/*
 * bgr2scc.cpp
 *
 * Loads a BGR graph once with our parallel reader, converts to all 3 SCC
 * algorithm formats in /dev/shm (RAM-backed tmpfs), then exits.
 * A shell script then runs each algorithm binary on the /dev/shm files.
 *
 * Output formats:
 *   /dev/shm/scc_wang.bin          — Wang et al. / GBBS binary
 *   /dev/shm/scc_ispan_fw_beg.bin — iSpan forward begin (int32)
 *   /dev/shm/scc_ispan_fw_csr.bin — iSpan forward CSR (int32)
 *   /dev/shm/scc_ispan_bw_beg.bin — iSpan backward begin (int32)
 *   /dev/shm/scc_ispan_bw_csr.bin — iSpan backward CSR (int32)
 *
 * Usage: ./bgr2scc <input.bgr>
 * Stdout: N<tab>M<tab>load_ms<tab>transpose_ms<tab>ispan_ok
 */

#include "graph.h"
#include "bgr_format.h"
#include "bgr_reader.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <omp.h>

static double now_ms() {
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void writeChunked(int fd, const void* data, size_t bytes, size_t offset) {
    size_t w = 0;
    while (w < bytes) {
        size_t chunk = std::min(bytes - w, (size_t)(1ULL << 30));
        pwrite(fd, (const char*)data + w, chunk, offset + w);
        w += chunk;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.bgr>\n";
        return 1;
    }

    // Load BGR with 64 threads
    omp_set_num_threads(64);
    double t0 = now_ms();
    Graph bgr = readBGR(std::string(argv[1]));
    double loadMs = now_ms() - t0;

    uint64_t N = bgr.numNodes, M = bgr.numEdges;
    std::cerr << "Loaded: N=" << N << " M=" << M << " in " << loadMs << "ms\n";

    // Build forward CSR: begin[N+1] (u64), adj[M] (u32)
    std::vector<uint64_t> fwd_begin(N + 1);
    std::vector<uint32_t> fwd_adj(M);
    fwd_begin[0] = 0;
    #pragma omp parallel for schedule(static)
    for (uint64_t i = 0; i < N; i++) fwd_begin[i + 1] = bgr.rowPtr[i];
    #pragma omp parallel for schedule(static)
    for (uint64_t i = 0; i < M; i++) fwd_adj[i] = (uint32_t)bgr.colIdx[i];
    bgr.clear();

    // Build reverse CSR
    std::cerr << "Building transpose...\n";
    double t1 = now_ms();
    std::vector<uint64_t> rev_begin(N + 1, 0);
    std::vector<uint32_t> rev_adj(M);
    #pragma omp parallel for schedule(static)
    for (uint64_t i = 0; i < M; i++)
        __sync_fetch_and_add(&rev_begin[fwd_adj[i] + 1], 1);
    for (uint64_t i = 1; i <= N; i++) rev_begin[i] += rev_begin[i - 1];
    std::vector<uint64_t> pos(rev_begin.begin(), rev_begin.end());
    for (uint64_t u = 0; u < N; u++)
        for (uint64_t e = fwd_begin[u]; e < fwd_begin[u + 1]; e++)
            rev_adj[pos[fwd_adj[e]]++] = (uint32_t)u;
    double transposeMs = now_ms() - t1;
    std::cerr << "Transpose: " << transposeMs << "ms\n";

    // === Write Wang/GBBS binary ===
    {
        const char* path = "/dev/shm/scc_wang.bin";
        uint64_t sizes = (N + 1) * 8 + M * 4 + 3 * 8;
        size_t total = 3 * 8 + (N + 1) * 8 + M * 4;
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ftruncate(fd, total);
        size_t off = 0;
        pwrite(fd, &N, 8, off); off += 8;
        pwrite(fd, &M, 8, off); off += 8;
        pwrite(fd, &sizes, 8, off); off += 8;
        writeChunked(fd, fwd_begin.data(), (N + 1) * 8, off); off += (N + 1) * 8;
        writeChunked(fd, fwd_adj.data(), M * 4, off);
        close(fd);
        std::cerr << "Wrote " << path << " (" << total / (1024*1024) << " MB)\n";
    }

    // === Write iSpan binaries (only if fits int32) ===
    bool ispan_ok = (M < 2147483647ULL && N < 2147483647ULL);
    if (ispan_ok) {
        std::vector<int32_t> fw_beg32(N + 1), bw_beg32(N + 1);
        std::vector<int32_t> fw_csr32(M), bw_csr32(M);
        #pragma omp parallel for
        for (uint64_t i = 0; i <= N; i++) {
            fw_beg32[i] = (int32_t)fwd_begin[i];
            bw_beg32[i] = (int32_t)rev_begin[i];
        }
        #pragma omp parallel for
        for (uint64_t i = 0; i < M; i++) {
            fw_csr32[i] = (int32_t)fwd_adj[i];
            bw_csr32[i] = (int32_t)rev_adj[i];
        }
        auto writeFile = [](const char* p, const void* d, size_t b) {
            int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            size_t w = 0;
            while (w < b) { size_t c = std::min(b-w,(size_t)(1ULL<<30)); pwrite(fd,(char*)d+w,c,w); w+=c; }
            close(fd);
        };
        writeFile("/dev/shm/scc_ispan_fw_beg.bin", fw_beg32.data(), (N+1)*4);
        writeFile("/dev/shm/scc_ispan_fw_csr.bin", fw_csr32.data(), M*4);
        writeFile("/dev/shm/scc_ispan_bw_beg.bin", bw_beg32.data(), (N+1)*4);
        writeFile("/dev/shm/scc_ispan_bw_csr.bin", bw_csr32.data(), M*4);
        std::cerr << "Wrote iSpan files\n";
    } else {
        std::cerr << "iSpan: SKIPPED (>2B edges)\n";
    }

    // Output: N, M, load_ms, transpose_ms, ispan_ok
    std::cout << N << "\t" << M << "\t" << loadMs << "\t" << transposeMs
              << "\t" << (ispan_ok ? "1" : "0") << std::endl;
    return 0;
}
