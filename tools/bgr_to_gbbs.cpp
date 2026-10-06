#include <algorithm>
#include <cerrno>
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
#include <vector>

#include <omp.h>

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

template <class T>
T loadUnaligned(const std::uint8_t* address) {
  T value;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

std::uint64_t checkedAdd(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    throw std::overflow_error("size addition overflow");
  return left + right;
}

std::uint64_t checkedMultiply(
    std::uint64_t left, std::uint64_t right) {
  if (right &&
      left > std::numeric_limits<std::uint64_t>::max() / right)
    throw std::overflow_error("size multiplication overflow");
  return left * right;
}

struct Mapping {
  int fd = -1;
  const std::uint8_t* data = nullptr;
  std::uint64_t bytes = 0;

  explicit Mapping(const std::string& path) {
    fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("cannot open BGR input");
    struct stat info {};
    if (::fstat(fd, &info) != 0 || info.st_size < 9)
      throw std::runtime_error("truncated BGR input");
    bytes = static_cast<std::uint64_t>(info.st_size);
    void* mapped =
        ::mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED)
      throw std::runtime_error("cannot mmap BGR input");
    data = static_cast<const std::uint8_t*>(mapped);
    ::madvise(const_cast<std::uint8_t*>(data), bytes, MADV_SEQUENTIAL);
  }

  ~Mapping() {
    if (data) ::munmap(const_cast<std::uint8_t*>(data), bytes);
    if (fd >= 0) ::close(fd);
  }
};

void writeExact(
    int fd,
    const void* data,
    std::size_t bytes,
    std::uint64_t offset) {
  const auto* source = static_cast<const std::uint8_t*>(data);
  std::size_t completed = 0;
  while (completed < bytes) {
    const ssize_t written = ::pwrite(
        fd,
        source + completed,
        bytes - completed,
        static_cast<off_t>(offset + completed));
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0)
      throw std::runtime_error(
          "pwrite failed: " + std::string(std::strerror(errno)));
    completed += static_cast<std::size_t>(written);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc != 4) {
    std::fprintf(
        stderr, "usage: %s INPUT.bgr OUTPUT.bin THREADS\n", argv[0]);
    return 2;
  }

  const std::string input_path = argv[1];
  const std::string output_path = argv[2];
  const int threads = std::stoi(argv[3]);
  if (threads <= 0 || threads > 256) {
    std::fprintf(stderr, "thread count must be in 1..256\n");
    return 2;
  }
  omp_set_num_threads(threads);

  try {
    const auto start = Clock::now();
    Mapping input(input_path);
    const std::uint8_t flags = input.data[0];
    if (flags & ~0x0bU)
      throw std::runtime_error("reserved BGR flags");
    const unsigned node_bytes = (flags & 1U) ? 8 : 4;
    const unsigned edge_bytes = (flags & 2U) ? 8 : 4;
    const std::uint64_t row_offset =
        1 + node_bytes + edge_bytes;
    const std::uint64_t n =
        node_bytes == 8
            ? loadUnaligned<std::uint64_t>(input.data + 1)
            : loadUnaligned<std::uint32_t>(input.data + 1);
    const std::uint64_t m =
        edge_bytes == 8
            ? loadUnaligned<std::uint64_t>(
                  input.data + 1 + node_bytes)
            : loadUnaligned<std::uint32_t>(
                  input.data + 1 + node_bytes);
    if (n > std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("GBBS output requires uint32 vertices");

    const std::uint64_t column_offset = checkedAdd(
        row_offset, checkedMultiply(n, edge_bytes));
    const std::uint64_t expected = checkedAdd(
        column_offset,
        checkedMultiply(m, node_bytes + ((flags & 8U) ? 4 : 0)));
    if (expected != input.bytes)
      throw std::runtime_error("BGR size mismatch");

    const std::uint64_t header_bytes = 3 * sizeof(std::uint64_t);
    const std::uint64_t offsets_bytes =
        checkedMultiply(n + 1, sizeof(std::uint64_t));
    const std::uint64_t destinations_bytes =
        checkedMultiply(m, sizeof(std::uint32_t));
    const std::uint64_t total_bytes = checkedAdd(
        header_bytes, checkedAdd(offsets_bytes, destinations_bytes));
    if (total_bytes >
        static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
      throw std::runtime_error("GBBS output exceeds off_t");

    const std::string partial = output_path + ".partial";
    if (::access(output_path.c_str(), F_OK) == 0 ||
        ::access(partial.c_str(), F_OK) == 0)
      throw std::runtime_error("output or partial file already exists");
    int output = ::open(
        partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (output < 0) throw std::runtime_error("cannot create output");

    try {
      const int allocation =
          ::posix_fallocate(output, 0, static_cast<off_t>(total_bytes));
      if (allocation != 0) {
        errno = allocation;
        throw std::runtime_error(
            "cannot reserve output: " +
            std::string(std::strerror(errno)));
      }

      const std::uint64_t sizes = total_bytes;
      writeExact(output, &n, sizeof(n), 0);
      writeExact(output, &m, sizeof(m), 8);
      writeExact(output, &sizes, sizeof(sizes), 16);
      const std::uint64_t zero = 0;
      writeExact(output, &zero, sizeof(zero), header_bytes);

      constexpr std::uint64_t chunk_bytes = 64ULL << 20;
      if (edge_bytes == 8) {
#pragma omp parallel for schedule(static)
        for (std::uint64_t byte = 0; byte < n * 8;
             byte += chunk_bytes) {
          const std::uint64_t count =
              std::min(chunk_bytes, n * 8 - byte);
          writeExact(
              output,
              input.data + row_offset + byte,
              static_cast<std::size_t>(count),
              header_bytes + 8 + byte);
        }
      } else {
#pragma omp parallel
        {
          std::vector<std::uint64_t> buffer(
              chunk_bytes / sizeof(std::uint64_t));
#pragma omp for schedule(static)
          for (std::uint64_t first = 0; first < n;
               first += buffer.size()) {
            const std::uint64_t count =
                std::min<std::uint64_t>(buffer.size(), n - first);
            for (std::uint64_t i = 0; i < count; ++i) {
              buffer[i] = loadUnaligned<std::uint32_t>(
                  input.data + row_offset + (first + i) * 4);
            }
            writeExact(
                output,
                buffer.data(),
                count * sizeof(std::uint64_t),
                header_bytes + 8 + first * 8);
          }
        }
      }

      const std::uint64_t output_columns =
          header_bytes + offsets_bytes;
      if (node_bytes == 4) {
#pragma omp parallel for schedule(static)
        for (std::uint64_t byte = 0; byte < destinations_bytes;
             byte += chunk_bytes) {
          const std::uint64_t count =
              std::min(chunk_bytes, destinations_bytes - byte);
          writeExact(
              output,
              input.data + column_offset + byte,
              static_cast<std::size_t>(count),
              output_columns + byte);
        }
      } else {
#pragma omp parallel
        {
          std::vector<std::uint32_t> buffer(
              chunk_bytes / sizeof(std::uint32_t));
#pragma omp for schedule(static)
          for (std::uint64_t first = 0; first < m;
               first += buffer.size()) {
            const std::uint64_t count =
                std::min<std::uint64_t>(buffer.size(), m - first);
            for (std::uint64_t i = 0; i < count; ++i) {
              const std::uint64_t destination =
                  loadUnaligned<std::uint64_t>(
                      input.data + column_offset + (first + i) * 8);
              if (destination >= n)
                throw std::runtime_error(
                    "BGR destination out of range");
              buffer[i] = static_cast<std::uint32_t>(destination);
            }
            writeExact(
                output,
                buffer.data(),
                count * sizeof(std::uint32_t),
                output_columns + first * 4);
          }
        }
      }

      if (::fsync(output) != 0)
        throw std::runtime_error("fsync failed");
      if (::close(output) != 0) {
        output = -1;
        throw std::runtime_error("close failed");
      }
      output = -1;
      if (::rename(partial.c_str(), output_path.c_str()) != 0)
        throw std::runtime_error("rename failed");
    } catch (...) {
      if (output >= 0) ::close(output);
      ::unlink(partial.c_str());
      throw;
    }

    std::cout << std::fixed << std::setprecision(9)
              << "BGR_TO_GBBS_RESULT {"
              << "\"input\":\"" << input_path << "\","
              << "\"output\":\"" << output_path << "\","
              << "\"vertices\":" << n << ","
              << "\"edges\":" << m << ","
              << "\"threads\":" << threads << ","
              << "\"output_bytes\":" << total_bytes << ","
              << "\"seconds\":" << secondsSince(start)
              << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "BGR_TO_GBBS_ERROR %s\n", error.what());
    return 1;
  }
}
