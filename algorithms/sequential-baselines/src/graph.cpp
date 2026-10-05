#include "scc/graph.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace scc {

struct BgrGraph::Impl {
  explicit Impl(std::string inputPath) : path(std::move(inputPath)) {
    const std::uint32_t endianProbe = 1;
    if (*reinterpret_cast<const std::uint8_t*>(&endianProbe) != 1) {
      throw std::runtime_error(
          "BGR reader requires a little-endian host");
    }

    try {
      fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
      if (fd < 0) {
        throw std::runtime_error(
            "cannot open input: " + std::string(std::strerror(errno)));
      }
      if (::fstat(fd, &identity) != 0 || identity.st_size < 9) {
        throw std::runtime_error("truncated or unreadable BGR file");
      }

      fileBytes = static_cast<std::uint64_t>(identity.st_size);
      std::uint8_t header[17]{};
      const ssize_t bytesRead = ::pread(fd, header, sizeof(header), 0);
      if (bytesRead < 9) {
        throw std::runtime_error("cannot read BGR header");
      }

      flags = header[0];
      if (flags & ~0x0bU) {
        throw std::runtime_error("BGR header contains reserved flags");
      }
      nodeBytes = (flags & 1U) ? 8U : 4U;
      edgeBytes = (flags & 2U) ? 8U : 4U;
      rowOffset = 1 + nodeBytes + edgeBytes;
      if (static_cast<std::uint64_t>(bytesRead) < rowOffset) {
        throw std::runtime_error("truncated BGR header");
      }

      vertices = nodeBytes == 8
                     ? loadUnaligned<std::uint64_t>(header + 1)
                     : loadUnaligned<std::uint32_t>(header + 1);
      edges = edgeBytes == 8
                  ? loadUnaligned<std::uint64_t>(
                        header + 1 + nodeBytes)
                  : loadUnaligned<std::uint32_t>(
                        header + 1 + nodeBytes);
      if (vertices > std::numeric_limits<Vertex>::max()) {
        throw std::runtime_error(
            "sequential baselines require at most 2^32-1 vertices");
      }

      columnOffset =
          checkedAdd(rowOffset, checkedMultiply(vertices, edgeBytes));
      csrBytes =
          checkedAdd(columnOffset, checkedMultiply(edges, nodeBytes));
      const std::uint64_t expectedBytes = checkedAdd(
          csrBytes, (flags & 8U) ? checkedMultiply(edges, 4) : 0);
      if (fileBytes != expectedBytes) {
        throw std::runtime_error(
            "BGR file size does not match its header");
      }

      void* mapped =
          ::mmap(nullptr, fileBytes, PROT_READ, MAP_PRIVATE, fd, 0);
      if (mapped == MAP_FAILED) {
        throw std::runtime_error(
            "cannot mmap BGR file: " +
            std::string(std::strerror(errno)));
      }
      data = static_cast<const std::uint8_t*>(mapped);
    } catch (...) {
      reset();
      throw;
    }
  }

  ~Impl() { reset(); }

  void advise(int advice) const {
    if (::madvise(
            const_cast<std::uint8_t*>(data), fileBytes, advice) != 0) {
      throw std::runtime_error(
          "madvise failed: " + std::string(std::strerror(errno)));
    }
  }

  GraphView view() const {
    return GraphView(
        data + rowOffset,
        data + columnOffset,
        vertices,
        edges,
        nodeBytes,
        edgeBytes);
  }

  void reset() {
    if (data) {
      ::munmap(const_cast<std::uint8_t*>(data), fileBytes);
      data = nullptr;
    }
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }

  std::string path;
  int fd = -1;
  const std::uint8_t* data = nullptr;
  struct stat identity {};
  std::uint64_t vertices = 0;
  std::uint64_t edges = 0;
  std::uint64_t fileBytes = 0;
  std::uint64_t csrBytes = 0;
  std::uint64_t rowOffset = 0;
  std::uint64_t columnOffset = 0;
  unsigned flags = 0;
  unsigned nodeBytes = 0;
  unsigned edgeBytes = 0;
};

BgrGraph::BgrGraph(std::string path)
    : impl_(std::make_unique<Impl>(std::move(path))) {}

BgrGraph::~BgrGraph() = default;
BgrGraph::BgrGraph(BgrGraph&&) noexcept = default;
BgrGraph& BgrGraph::operator=(BgrGraph&&) noexcept = default;

GraphView BgrGraph::view() const { return impl_->view(); }

void BgrGraph::validate() const {
  impl_->advise(MADV_SEQUENTIAL);
  const GraphView graph = view();
  EdgeIndex previous = 0;
  for (std::uint64_t vertex = 0;
       vertex < graph.vertexCount(); ++vertex) {
    const EdgeIndex end =
        graph.edgeEnd(static_cast<Vertex>(vertex));
    if (end < previous || end > graph.edgeCount()) {
      throw std::runtime_error(
          "invalid cumulative row end at vertex " +
          std::to_string(vertex));
    }
    previous = end;
  }
  if (previous != graph.edgeCount()) {
    throw std::runtime_error(
        "final BGR row end differs from edge count");
  }
  for (EdgeIndex edge = 0; edge < graph.edgeCount(); ++edge) {
    (void)graph.destination(edge);
  }
}

void BgrGraph::prepareForScc() const { impl_->advise(MADV_RANDOM); }

void BgrGraph::assertUnchanged() const {
  struct stat current {};
  if (::fstat(impl_->fd, &current) != 0 ||
      current.st_size != impl_->identity.st_size ||
      current.st_mtim.tv_sec != impl_->identity.st_mtim.tv_sec ||
      current.st_mtim.tv_nsec != impl_->identity.st_mtim.tv_nsec ||
      current.st_ctim.tv_sec != impl_->identity.st_ctim.tv_sec ||
      current.st_ctim.tv_nsec != impl_->identity.st_ctim.tv_nsec) {
    throw std::runtime_error(
        "input BGR changed during SCC analysis");
  }
}

unsigned BgrGraph::flags() const { return impl_->flags; }
bool BgrGraph::weighted() const { return (impl_->flags & 8U) != 0; }
const std::string& BgrGraph::path() const { return impl_->path; }
std::uint64_t BgrGraph::mappedBytes() const {
  return impl_->fileBytes;
}

}  // namespace scc
