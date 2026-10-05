// Exact, iterative Tarjan and Gabow SCC baselines for BGR v2 graphs.
// Linux, C++17, little-endian, 64-bit file offsets.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
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
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using EdgeIndex = uint64_t;
using Vertex = uint32_t;

constexpr uint64_t kNoOrder = std::numeric_limits<uint64_t>::max();
constexpr Vertex kNoVertex = std::numeric_limits<Vertex>::max();
constexpr uint64_t kProgressInterval = 1ULL << 28;
constexpr uint64_t kDeadlineCheckInterval = 1ULL << 20;

static_assert(sizeof(size_t) == 8 && sizeof(off_t) == 8,
              "a 64-bit platform is required");
static_assert(
    sizeof(EdgeIndex) == 8 &&
        std::numeric_limits<EdgeIndex>::max() > uint64_t{91792261600ULL},
    "edge indices must represent graphs larger than 2^32 edges");

double elapsedSeconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

uint64_t checkedAdd(uint64_t left, uint64_t right) {
  if (right > std::numeric_limits<uint64_t>::max() - left) {
    throw std::overflow_error("64-bit addition overflow");
  }
  return left + right;
}

uint64_t checkedMultiply(uint64_t left, uint64_t right) {
  if (right && left > std::numeric_limits<uint64_t>::max() / right) {
    throw std::overflow_error("64-bit multiplication overflow");
  }
  return left * right;
}

template <class T>
T loadUnaligned(const uint8_t* address) {
  T value;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

std::string jsonQuote(const std::string& value) {
  std::string output = "\"";
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20) {
          char escaped[7];
          std::snprintf(
              escaped, sizeof(escaped), "\\u%04x",
              static_cast<unsigned>(character));
          output += escaped;
        } else {
          output += static_cast<char>(character);
        }
    }
  }
  return output + '"';
}

class MappedBgr {
 public:
  explicit MappedBgr(std::string path) : path_(std::move(path)) {
    const uint32_t endianProbe = 1;
    if (*reinterpret_cast<const uint8_t*>(&endianProbe) != 1) {
      throw std::runtime_error("BGR reader requires a little-endian host");
    }

    try {
      fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
      if (fd_ < 0) {
        throw std::runtime_error(
            "cannot open input: " + std::string(std::strerror(errno)));
      }
      if (::fstat(fd_, &identity_) != 0 || identity_.st_size < 9) {
        throw std::runtime_error("truncated or unreadable BGR file");
      }

      fileBytes_ = static_cast<uint64_t>(identity_.st_size);
      uint8_t header[17]{};
      const ssize_t bytesRead = ::pread(fd_, header, sizeof(header), 0);
      if (bytesRead < 9) {
        throw std::runtime_error("cannot read BGR header");
      }

      flags_ = header[0];
      if (flags_ & ~0x0bU) {
        throw std::runtime_error("BGR header contains reserved flags");
      }
      nodeBytes_ = (flags_ & 1U) ? 8U : 4U;
      edgeBytes_ = (flags_ & 2U) ? 8U : 4U;
      rowOffset_ = 1 + nodeBytes_ + edgeBytes_;
      if (static_cast<uint64_t>(bytesRead) < rowOffset_) {
        throw std::runtime_error("truncated BGR header");
      }

      vertices_ = nodeBytes_ == 8
                      ? loadUnaligned<uint64_t>(header + 1)
                      : loadUnaligned<uint32_t>(header + 1);
      edges_ = edgeBytes_ == 8
                   ? loadUnaligned<uint64_t>(header + 1 + nodeBytes_)
                   : loadUnaligned<uint32_t>(header + 1 + nodeBytes_);
      if (vertices_ > std::numeric_limits<Vertex>::max()) {
        throw std::runtime_error(
            "sequential baselines require at most 2^32-1 vertices");
      }

      columnOffset_ =
          checkedAdd(rowOffset_, checkedMultiply(vertices_, edgeBytes_));
      csrBytes_ =
          checkedAdd(columnOffset_, checkedMultiply(edges_, nodeBytes_));
      const uint64_t expectedBytes = checkedAdd(
          csrBytes_, (flags_ & 8U) ? checkedMultiply(edges_, 4) : 0);
      if (fileBytes_ != expectedBytes) {
        throw std::runtime_error("BGR file size does not match its header");
      }

      void* mapped =
          ::mmap(nullptr, fileBytes_, PROT_READ, MAP_PRIVATE, fd_, 0);
      if (mapped == MAP_FAILED) {
        throw std::runtime_error(
            "cannot mmap BGR file: " + std::string(std::strerror(errno)));
      }
      data_ = static_cast<const uint8_t*>(mapped);
    } catch (...) {
      reset();
      throw;
    }
  }

  ~MappedBgr() { reset(); }

  MappedBgr(const MappedBgr&) = delete;
  MappedBgr& operator=(const MappedBgr&) = delete;

  uint64_t vertexCount() const { return vertices_; }
  uint64_t edgeCount() const { return edges_; }
  unsigned flags() const { return flags_; }
  bool weighted() const { return (flags_ & 8U) != 0; }
  const std::string& path() const { return path_; }

  EdgeIndex edgeBegin(Vertex vertex) const {
    return vertex ? edgeEnd(vertex - 1) : 0;
  }

  EdgeIndex edgeEnd(Vertex vertex) const {
    const uint8_t* value =
        data_ + rowOffset_ + static_cast<uint64_t>(vertex) * edgeBytes_;
    return edgeBytes_ == 8 ? loadUnaligned<uint64_t>(value)
                           : loadUnaligned<uint32_t>(value);
  }

  Vertex destination(EdgeIndex edge) const {
    const uint8_t* value = data_ + columnOffset_ + edge * nodeBytes_;
    const uint64_t destination =
        nodeBytes_ == 8 ? loadUnaligned<uint64_t>(value)
                        : loadUnaligned<uint32_t>(value);
    if (destination >= vertices_) {
      throw std::runtime_error(
          "BGR destination out of range at edge " + std::to_string(edge));
    }
    return static_cast<Vertex>(destination);
  }

  void validate() const {
    advise(MADV_SEQUENTIAL);
    EdgeIndex previous = 0;
    for (uint64_t vertex = 0; vertex < vertices_; ++vertex) {
      const EdgeIndex end = edgeEnd(static_cast<Vertex>(vertex));
      if (end < previous || end > edges_) {
        throw std::runtime_error(
            "invalid cumulative row end at vertex " +
            std::to_string(vertex));
      }
      previous = end;
    }
    if (previous != edges_) {
      throw std::runtime_error("final BGR row end differs from edge count");
    }
    for (EdgeIndex edge = 0; edge < edges_; ++edge) {
      (void)destination(edge);
    }
  }

  void prepareForScc() const { advise(MADV_RANDOM); }

  void assertUnchanged() const {
    struct stat current {};
    if (::fstat(fd_, &current) != 0 ||
        current.st_size != identity_.st_size ||
        current.st_mtim.tv_sec != identity_.st_mtim.tv_sec ||
        current.st_mtim.tv_nsec != identity_.st_mtim.tv_nsec ||
        current.st_ctim.tv_sec != identity_.st_ctim.tv_sec ||
        current.st_ctim.tv_nsec != identity_.st_ctim.tv_nsec) {
      throw std::runtime_error("input BGR changed during SCC analysis");
    }
  }

 private:
  void advise(int advice) const {
    if (::madvise(const_cast<uint8_t*>(data_), fileBytes_, advice) != 0) {
      throw std::runtime_error(
          "madvise failed: " + std::string(std::strerror(errno)));
    }
  }

  void reset() {
    if (data_) {
      ::munmap(const_cast<uint8_t*>(data_), fileBytes_);
      data_ = nullptr;
    }
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  std::string path_;
  int fd_ = -1;
  const uint8_t* data_ = nullptr;
  struct stat identity_ {};
  uint64_t vertices_ = 0;
  uint64_t edges_ = 0;
  uint64_t fileBytes_ = 0;
  uint64_t csrBytes_ = 0;
  uint64_t rowOffset_ = 0;
  uint64_t columnOffset_ = 0;
  unsigned flags_ = 0;
  unsigned nodeBytes_ = 0;
  unsigned edgeBytes_ = 0;
};

struct MemoryGraph {
  uint64_t vertices = 0;
  std::vector<EdgeIndex> rowEnds;
  std::vector<Vertex> destinations;

  uint64_t vertexCount() const { return vertices; }
  uint64_t edgeCount() const { return destinations.size(); }
  EdgeIndex edgeBegin(Vertex vertex) const {
    return vertex ? rowEnds[vertex - 1] : 0;
  }
  EdgeIndex edgeEnd(Vertex vertex) const { return rowEnds[vertex]; }
  Vertex destination(EdgeIndex edge) const { return destinations[edge]; }
};

class Deadline {
 public:
  Deadline(double seconds, Clock::time_point start)
      : seconds_(seconds), start_(start) {}

  void check(uint64_t edgesVisited, bool force = false) const {
    if (seconds_ <= 0) return;
    if (!force &&
        (edgesVisited & (kDeadlineCheckInterval - 1)) != 0) {
      return;
    }
    if (elapsedSeconds(start_) > seconds_) {
      throw std::runtime_error("SCC time limit exceeded");
    }
  }

 private:
  double seconds_;
  Clock::time_point start_;
};

struct SccResult {
  std::unique_ptr<Vertex[]> labels;
  uint64_t components = 0;
  uint64_t largest = 0;
  uint64_t scannedEdges = 0;
  uint64_t discoveredVertices = 0;
  uint64_t assignedVertices = 0;
};

struct Frame {
  EdgeIndex nextEdge;
  Vertex vertex;
};

void reportProgress(
    const char* algorithm,
    const SccResult& result,
    Clock::time_point start) {
  if (result.scannedEdges == 0 ||
      (result.scannedEdges & (kProgressInterval - 1)) != 0) {
    return;
  }
  std::cerr << "SCC_PROGRESS {"
            << "\"algorithm\":" << jsonQuote(algorithm) << ","
            << "\"seconds\":" << elapsedSeconds(start) << ","
            << "\"scanned_edges\":" << result.scannedEdges << ","
            << "\"discovered_vertices\":" << result.discoveredVertices << ","
            << "\"assigned_vertices\":" << result.assignedVertices << ","
            << "\"components\":" << result.components
            << "}\n";
}

template <class Graph>
SccResult runTarjan(const Graph& graph, double limitSeconds) {
  const auto start = Clock::now();
  const Deadline deadline(limitSeconds, start);
  const uint64_t vertexCount = graph.vertexCount();
  SccResult result;
  result.labels.reset(new Vertex[vertexCount]);
  std::fill_n(result.labels.get(), vertexCount, kNoVertex);
  if (vertexCount == 0) return result;

  std::unique_ptr<uint64_t[]> order(new uint64_t[vertexCount]);
  std::unique_ptr<uint64_t[]> low(new uint64_t[vertexCount]);
  std::unique_ptr<Vertex[]> active(new Vertex[vertexCount]);
  std::unique_ptr<Frame[]> dfs(new Frame[vertexCount]);
  std::fill_n(order.get(), vertexCount, kNoOrder);

  uint64_t nextOrder = 0;
  uint64_t activeSize = 0;
  uint64_t depth = 0;

  auto discover = [&](Vertex vertex) {
    order[vertex] = low[vertex] = nextOrder++;
    active[activeSize++] = vertex;
    dfs[depth++] = Frame{graph.edgeBegin(vertex), vertex};
    result.discoveredVertices = nextOrder;
  };

  for (uint64_t root = 0; root < vertexCount; ++root) {
    if (order[root] != kNoOrder) continue;
    deadline.check(result.scannedEdges, true);
    discover(static_cast<Vertex>(root));

    while (depth) {
      Frame& frame = dfs[depth - 1];
      const Vertex vertex = frame.vertex;
      const EdgeIndex end = graph.edgeEnd(vertex);
      if (frame.nextEdge < end) {
        const Vertex destination = graph.destination(frame.nextEdge++);
        ++result.scannedEdges;
        deadline.check(result.scannedEdges);
        reportProgress("tarjan", result, start);
        if (order[destination] == kNoOrder) {
          discover(destination);
        } else if (result.labels[destination] == kNoVertex) {
          low[vertex] = std::min(low[vertex], order[destination]);
        }
        continue;
      }

      if (low[vertex] == order[vertex]) {
        const uint64_t oldActiveSize = activeSize;
        Vertex minimum = kNoVertex;
        Vertex member;
        do {
          if (activeSize == 0) {
            throw std::runtime_error("Tarjan active-stack underflow");
          }
          member = active[--activeSize];
          minimum = std::min(minimum, member);
        } while (member != vertex);

        for (uint64_t index = activeSize; index < oldActiveSize; ++index) {
          result.labels[active[index]] = minimum;
        }
        const uint64_t componentSize = oldActiveSize - activeSize;
        result.largest = std::max(result.largest, componentSize);
        result.assignedVertices += componentSize;
        ++result.components;
      }

      --depth;
      if (depth) {
        const Vertex parent = dfs[depth - 1].vertex;
        low[parent] = std::min(low[parent], low[vertex]);
      }
    }
  }

  if (result.discoveredVertices != vertexCount ||
      result.assignedVertices != vertexCount ||
      result.scannedEdges != graph.edgeCount() || activeSize != 0) {
    throw std::runtime_error("Tarjan coverage invariant failed");
  }
  return result;
}

template <class Graph>
SccResult runGabow(const Graph& graph, double limitSeconds) {
  const auto start = Clock::now();
  const Deadline deadline(limitSeconds, start);
  const uint64_t vertexCount = graph.vertexCount();
  SccResult result;
  result.labels.reset(new Vertex[vertexCount]);
  std::fill_n(result.labels.get(), vertexCount, kNoVertex);
  if (vertexCount == 0) return result;

  std::unique_ptr<uint64_t[]> order(new uint64_t[vertexCount]);
  std::unique_ptr<Vertex[]> active(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> roots(new Vertex[vertexCount]);
  std::unique_ptr<Frame[]> dfs(new Frame[vertexCount]);
  std::fill_n(order.get(), vertexCount, kNoOrder);

  uint64_t nextOrder = 0;
  uint64_t activeSize = 0;
  uint64_t rootsSize = 0;
  uint64_t depth = 0;

  auto discover = [&](Vertex vertex) {
    order[vertex] = nextOrder++;
    active[activeSize++] = vertex;
    roots[rootsSize++] = vertex;
    dfs[depth++] = Frame{graph.edgeBegin(vertex), vertex};
    result.discoveredVertices = nextOrder;
  };

  for (uint64_t root = 0; root < vertexCount; ++root) {
    if (order[root] != kNoOrder) continue;
    deadline.check(result.scannedEdges, true);
    discover(static_cast<Vertex>(root));

    while (depth) {
      Frame& frame = dfs[depth - 1];
      const Vertex vertex = frame.vertex;
      const EdgeIndex end = graph.edgeEnd(vertex);
      if (frame.nextEdge < end) {
        const Vertex destination = graph.destination(frame.nextEdge++);
        ++result.scannedEdges;
        deadline.check(result.scannedEdges);
        reportProgress("gabow", result, start);
        if (order[destination] == kNoOrder) {
          discover(destination);
        } else if (result.labels[destination] == kNoVertex) {
          while (rootsSize &&
                 order[roots[rootsSize - 1]] > order[destination]) {
            --rootsSize;
          }
          if (rootsSize == 0) {
            throw std::runtime_error("Gabow root-stack underflow");
          }
        }
        continue;
      }

      if (rootsSize && roots[rootsSize - 1] == vertex) {
        --rootsSize;
        const uint64_t oldActiveSize = activeSize;
        Vertex minimum = kNoVertex;
        Vertex member;
        do {
          if (activeSize == 0) {
            throw std::runtime_error("Gabow active-stack underflow");
          }
          member = active[--activeSize];
          minimum = std::min(minimum, member);
        } while (member != vertex);

        for (uint64_t index = activeSize; index < oldActiveSize; ++index) {
          result.labels[active[index]] = minimum;
        }
        const uint64_t componentSize = oldActiveSize - activeSize;
        result.largest = std::max(result.largest, componentSize);
        result.assignedVertices += componentSize;
        ++result.components;
      }
      --depth;
    }
  }

  if (result.discoveredVertices != vertexCount ||
      result.assignedVertices != vertexCount ||
      result.scannedEdges != graph.edgeCount() || activeSize != 0 ||
      rootsSize != 0) {
    throw std::runtime_error("Gabow coverage invariant failed");
  }
  return result;
}

/*
 * The Pearce and Tarjan-Zwick implementations below are C++ adaptations of
 * scipy/sparse/csgraph/_traversal.pyx. SciPy is distributed under the BSD
 * 3-Clause license; see SCIPY_LICENSE.txt. The adaptations use checked BGR
 * access, 64-bit edge positions, deadlines, progress reporting, and canonical
 * minimum-vertex component labels.
 */
template <class Graph>
SccResult runPearce(const Graph& graph, double limitSeconds) {
  const auto start = Clock::now();
  const Deadline deadline(limitSeconds, start);
  const uint64_t vertexCount = graph.vertexCount();
  constexpr Vertex kStackEnd = kNoVertex - 1;
  if (vertexCount >= kStackEnd) {
    throw std::runtime_error(
        "Pearce requires fewer than 2^32-2 vertices");
  }

  SccResult result;
  result.labels.reset(new Vertex[vertexCount]);
  std::fill_n(result.labels.get(), vertexCount, kNoVertex);
  if (vertexCount == 0) return result;

  std::unique_ptr<Vertex[]> lowLink(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> sharedLink(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> stackBackward(new Vertex[vertexCount]);
  std::fill_n(lowLink.get(), vertexCount, kNoVertex);
  std::fill_n(sharedLink.get(), vertexCount, kNoVertex);
  std::fill_n(stackBackward.get(), vertexCount, kNoVertex);

  Vertex componentStackHead = kStackEnd;
  Vertex dfsStackHead = kStackEnd;
  uint64_t index = 0;

  for (uint64_t root = 0; root < vertexCount; ++root) {
    if (lowLink[root] != kNoVertex) continue;
    deadline.check(result.scannedEdges, true);

    dfsStackHead = static_cast<Vertex>(root);
    sharedLink[root] = kStackEnd;
    stackBackward[root] = kStackEnd;

    while (dfsStackHead != kStackEnd) {
      const Vertex vertex = dfsStackHead;
      if (lowLink[vertex] == kNoVertex) {
        lowLink[vertex] = static_cast<Vertex>(index++);
        ++result.discoveredVertices;

        for (EdgeIndex edge = graph.edgeBegin(vertex);
             edge < graph.edgeEnd(vertex); ++edge) {
          const Vertex destination = graph.destination(edge);
          ++result.scannedEdges;
          deadline.check(result.scannedEdges);
          reportProgress("pearce", result, start);
          if (lowLink[destination] != kNoVertex) continue;
          if (destination == dfsStackHead) continue;

          if (sharedLink[destination] != kNoVertex) {
            const Vertex forward = sharedLink[destination];
            const Vertex backward = stackBackward[destination];
            if (backward != kStackEnd) {
              sharedLink[backward] = forward;
            }
            if (forward != kStackEnd) {
              stackBackward[forward] = backward;
            }
          }

          sharedLink[destination] = dfsStackHead;
          stackBackward[destination] = kStackEnd;
          stackBackward[dfsStackHead] = destination;
          dfsStackHead = destination;
        }
        continue;
      }

      dfsStackHead = sharedLink[vertex];
      if (dfsStackHead != kStackEnd) {
        stackBackward[dfsStackHead] = kStackEnd;
      }
      sharedLink[vertex] = kNoVertex;
      stackBackward[vertex] = kNoVertex;

      bool isRoot = true;
      Vertex low = lowLink[vertex];
      for (EdgeIndex edge = graph.edgeBegin(vertex);
           edge < graph.edgeEnd(vertex); ++edge) {
        const Vertex destination = graph.destination(edge);
        ++result.scannedEdges;
        deadline.check(result.scannedEdges);
        reportProgress("pearce", result, start);
        if (result.labels[destination] == kNoVertex &&
            lowLink[destination] < low) {
          low = lowLink[destination];
          isRoot = false;
        }
      }
      lowLink[vertex] = low;

      if (!isRoot) {
        sharedLink[vertex] = componentStackHead;
        componentStackHead = vertex;
        continue;
      }

      if (index == 0) {
        throw std::runtime_error("Pearce index underflow");
      }
      --index;
      const Vertex oldHead = componentStackHead;
      Vertex newHead = oldHead;
      Vertex minimum = vertex;
      uint64_t componentSize = 1;
      while (newHead != kStackEnd &&
             lowLink[vertex] <= lowLink[newHead]) {
        minimum = std::min(minimum, newHead);
        ++componentSize;
        newHead = sharedLink[newHead];
        if (index == 0) {
          throw std::runtime_error("Pearce index underflow");
        }
        --index;
      }

      Vertex member = oldHead;
      while (member != newHead) {
        const Vertex next = sharedLink[member];
        result.labels[member] = minimum;
        sharedLink[member] = kNoVertex;
        member = next;
      }
      componentStackHead = newHead;
      result.labels[vertex] = minimum;
      result.assignedVertices += componentSize;
      result.largest = std::max(result.largest, componentSize);
      ++result.components;
    }
  }

  if (result.discoveredVertices != vertexCount ||
      result.assignedVertices != vertexCount ||
      result.scannedEdges != checkedMultiply(graph.edgeCount(), 2) ||
      componentStackHead != kStackEnd || index != 0) {
    throw std::runtime_error("Pearce coverage invariant failed");
  }
  return result;
}

template <class Graph>
SccResult runTarjanZwick(const Graph& graph, double limitSeconds) {
  const auto start = Clock::now();
  const Deadline deadline(limitSeconds, start);
  const uint64_t vertexCount = graph.vertexCount();
  constexpr Vertex kNonLead = Vertex{1} << 31;
  constexpr Vertex kNodeMask = kNonLead - 1;
  if (vertexCount > kNodeMask) {
    throw std::runtime_error(
        "Tarjan-Zwick requires at most 2^31-1 vertices");
  }

  SccResult result;
  result.labels.reset(new Vertex[vertexCount]);
  std::fill_n(result.labels.get(), vertexCount, kNoVertex);
  if (vertexCount == 0) return result;

  constexpr EdgeIndex kUnvisited = std::numeric_limits<EdgeIndex>::max();
  std::unique_ptr<EdgeIndex[]> successorPosition(
      new EdgeIndex[vertexCount]);
  std::unique_ptr<Vertex[]> highLink(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> stack(new Vertex[vertexCount]);
  std::fill_n(successorPosition.get(), vertexCount, kUnvisited);

  uint64_t dfsSize = 0;
  uint64_t componentSize = 0;
  uint64_t index = vertexCount;
  bool done = false;

  auto decode = [&](Vertex encoded) { return encoded & kNodeMask; };
  auto pushDfs = [&](Vertex encoded) {
    if (dfsSize + componentSize >= vertexCount) {
      throw std::runtime_error("Tarjan-Zwick stack overflow");
    }
    stack[dfsSize++] = encoded;
  };
  auto pushComponent = [&](Vertex vertex) {
    if (dfsSize + componentSize >= vertexCount) {
      throw std::runtime_error("Tarjan-Zwick stack overflow");
    }
    stack[vertexCount - ++componentSize] = vertex;
  };
  auto componentTop = [&]() {
    return stack[vertexCount - componentSize];
  };

  for (uint64_t root = 0; root < vertexCount; ++root) {
    if (successorPosition[root] != kUnvisited) continue;
    deadline.check(result.scannedEdges, true);

    const Vertex rootHighLink = static_cast<Vertex>(index);
    const Vertex rootVertex = static_cast<Vertex>(root);
    pushDfs(rootVertex);
    highLink[rootVertex] = static_cast<Vertex>(index--);
    successorPosition[rootVertex] = graph.edgeBegin(rootVertex);
    ++result.discoveredVertices;

    while (dfsSize) {
      const Vertex vertex = decode(stack[dfsSize - 1]);
      if (successorPosition[vertex] < graph.edgeEnd(vertex)) {
        const Vertex destination =
            graph.destination(successorPosition[vertex]++);
        ++result.scannedEdges;
        deadline.check(result.scannedEdges);
        reportProgress("tarjan-zwick", result, start);

        if (successorPosition[destination] == kUnvisited) {
          pushDfs(destination);
          highLink[destination] = static_cast<Vertex>(index--);
          successorPosition[destination] =
              graph.edgeBegin(destination);
          ++result.discoveredVertices;
        } else if (result.labels[destination] == kNoVertex &&
                   highLink[vertex] < highLink[destination]) {
          stack[dfsSize - 1] = vertex | kNonLead;
          highLink[vertex] = highLink[destination];

          if (highLink[vertex] == rootHighLink && index == 0) {
            std::fill_n(result.labels.get(), vertexCount, Vertex{0});
            result.components = 1;
            result.largest = vertexCount;
            result.assignedVertices = vertexCount;
            dfsSize = 0;
            componentSize = 0;
            done = true;
            break;
          }
        }
        continue;
      }

      const bool isLead = (stack[dfsSize - 1] & kNonLead) == 0;
      if (isLead) {
        --dfsSize;
        const uint64_t oldComponentSize = componentSize;
        Vertex minimum = vertex;
        while (componentSize) {
          const Vertex top = componentTop();
          if (highLink[vertex] < highLink[top]) break;
          minimum = std::min(minimum, top);
          --componentSize;
          ++index;
        }

        const uint64_t first = vertexCount - oldComponentSize;
        const uint64_t last = vertexCount - componentSize;
        for (uint64_t position = first; position < last; ++position) {
          result.labels[stack[position]] = minimum;
        }
        result.labels[vertex] = minimum;
        ++index;

        const uint64_t emittedSize =
            1 + oldComponentSize - componentSize;
        result.assignedVertices += emittedSize;
        result.largest = std::max(result.largest, emittedSize);
        ++result.components;
      } else {
        --dfsSize;
        pushComponent(vertex);
        if (dfsSize) {
          const Vertex parent = decode(stack[dfsSize - 1]);
          if (highLink[parent] < highLink[vertex]) {
            stack[dfsSize - 1] = parent | kNonLead;
            highLink[parent] = highLink[vertex];
          }
        }
      }
    }
    if (done) break;
  }

  if (result.discoveredVertices != vertexCount ||
      result.assignedVertices != vertexCount || dfsSize != 0 ||
      componentSize != 0 || result.scannedEdges > graph.edgeCount()) {
    throw std::runtime_error("Tarjan-Zwick coverage invariant failed");
  }
  return result;
}

template <class Graph>
SccResult runAlgorithm(
    const Graph& graph,
    const std::string& algorithm,
    double limitSeconds) {
  if (algorithm == "tarjan") {
    return runTarjan(graph, limitSeconds);
  }
  if (algorithm == "gabow") {
    return runGabow(graph, limitSeconds);
  }
  if (algorithm == "pearce") {
    return runPearce(graph, limitSeconds);
  }
  if (algorithm == "tarjan-zwick") {
    return runTarjanZwick(graph, limitSeconds);
  }
  throw std::runtime_error(
      "algorithm must be 'tarjan', 'gabow', 'pearce', or "
      "'tarjan-zwick'");
}

void writeExact(
    int fd,
    const void* data,
    size_t bytes,
    uint64_t offset) {
  const auto* source = static_cast<const uint8_t*>(data);
  size_t completed = 0;
  while (completed < bytes) {
    const ssize_t written = ::pwrite(
        fd, source + completed, bytes - completed,
        static_cast<off_t>(offset + completed));
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      throw std::runtime_error(
          "label write failed: " + std::string(std::strerror(errno)));
    }
    completed += static_cast<size_t>(written);
  }
}

void writeLabels(
    const std::string& output,
    const Vertex* labels,
    uint64_t vertexCount) {
  if (output == "-") return;
  const uint64_t bytes = checkedMultiply(vertexCount, sizeof(Vertex));
  if (bytes > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
    throw std::runtime_error("label output exceeds platform file limit");
  }

  const std::string partial =
      output + ".partial." + std::to_string(::getpid());
  int fd = ::open(
      partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error(
        "cannot create label output: " + std::string(std::strerror(errno)));
  }
  try {
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
      throw std::runtime_error("cannot size label output");
    }
    constexpr uint64_t kChunkBytes = 1ULL << 30;
    for (uint64_t offset = 0; offset < bytes; offset += kChunkBytes) {
      const uint64_t count = std::min(kChunkBytes, bytes - offset);
      writeExact(
          fd, reinterpret_cast<const uint8_t*>(labels) + offset,
          static_cast<size_t>(count), offset);
    }
    if (::fsync(fd) != 0) {
      throw std::runtime_error("cannot fsync label output");
    }
    if (::close(fd) != 0) {
      fd = -1;
      throw std::runtime_error("cannot close label output");
    }
    fd = -1;
    if (::rename(partial.c_str(), output.c_str()) != 0) {
      throw std::runtime_error(
          "cannot publish label output: " +
          std::string(std::strerror(errno)));
    }
  } catch (...) {
    if (fd >= 0) ::close(fd);
    ::unlink(partial.c_str());
    throw;
  }
}

MemoryGraph graphFromMask(unsigned vertices, uint64_t mask) {
  MemoryGraph graph;
  graph.vertices = vertices;
  graph.rowEnds.resize(vertices);
  for (unsigned source = 0; source < vertices; ++source) {
    for (unsigned destination = 0; destination < vertices; ++destination) {
      const unsigned bit = source * vertices + destination;
      if ((mask >> bit) & 1ULL) {
        graph.destinations.push_back(destination);
      }
    }
    graph.rowEnds[source] = graph.destinations.size();
  }
  return graph;
}

MemoryGraph randomGraph(unsigned vertices, uint64_t& state) {
  MemoryGraph graph;
  graph.vertices = vertices;
  graph.rowEnds.resize(vertices);
  auto next = [&]() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  };
  for (unsigned source = 0; source < vertices; ++source) {
    for (unsigned destination = 0; destination < vertices; ++destination) {
      if ((next() & 3U) == 0) {
        graph.destinations.push_back(destination);
      }
    }
    graph.rowEnds[source] = graph.destinations.size();
  }
  return graph;
}

std::vector<Vertex> referenceLabels(const MemoryGraph& graph) {
  const size_t vertices = graph.vertexCount();
  std::vector<uint8_t> reachable(vertices * vertices, 0);
  for (size_t vertex = 0; vertex < vertices; ++vertex) {
    reachable[vertex * vertices + vertex] = 1;
    for (EdgeIndex edge = graph.edgeBegin(vertex);
         edge < graph.edgeEnd(vertex); ++edge) {
      reachable[vertex * vertices + graph.destination(edge)] = 1;
    }
  }
  for (size_t middle = 0; middle < vertices; ++middle) {
    for (size_t source = 0; source < vertices; ++source) {
      if (!reachable[source * vertices + middle]) continue;
      for (size_t destination = 0; destination < vertices; ++destination) {
        reachable[source * vertices + destination] |=
            reachable[middle * vertices + destination];
      }
    }
  }

  std::vector<Vertex> labels(vertices, kNoVertex);
  for (size_t vertex = 0; vertex < vertices; ++vertex) {
    Vertex minimum = static_cast<Vertex>(vertex);
    for (size_t candidate = 0; candidate < vertices; ++candidate) {
      if (reachable[vertex * vertices + candidate] &&
          reachable[candidate * vertices + vertex]) {
        minimum = std::min(minimum, static_cast<Vertex>(candidate));
      }
    }
    labels[vertex] = minimum;
  }
  return labels;
}

void assertLabels(
    const MemoryGraph& graph,
    const std::vector<Vertex>& expected,
    const char* context) {
  for (const char* algorithm :
       {"tarjan", "gabow", "pearce", "tarjan-zwick"}) {
    SccResult actual = runAlgorithm(graph, algorithm, 0);
    for (size_t vertex = 0; vertex < expected.size(); ++vertex) {
      if (actual.labels[vertex] != expected[vertex]) {
        throw std::runtime_error(
            std::string(context) + ": " + algorithm +
            " disagrees at vertex " + std::to_string(vertex));
      }
    }
  }
}

void runSelfTest() {
  uint64_t cases = 0;
  for (uint64_t mask = 0; mask < (1ULL << 16); ++mask) {
    const MemoryGraph graph = graphFromMask(4, mask);
    assertLabels(graph, referenceLabels(graph), "four-vertex oracle");
    ++cases;
  }

  uint64_t randomState = 0x6a09e667f3bcc909ULL;
  for (uint64_t test = 0; test < 2048; ++test) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    const unsigned vertices = 5 + randomState % 12;
    const MemoryGraph graph = randomGraph(vertices, randomState);
    assertLabels(graph, referenceLabels(graph), "random graph oracle");
    ++cases;
  }

  {
    const MemoryGraph empty;
    assertLabels(empty, {}, "empty graph");
    ++cases;
  }
  {
    MemoryGraph duplicateEdges;
    duplicateEdges.vertices = 3;
    duplicateEdges.rowEnds = {2, 5, 6};
    duplicateEdges.destinations = {1, 1, 0, 0, 2, 2};
    assertLabels(duplicateEdges, {0, 0, 2}, "duplicate edges");
    ++cases;
  }
  {
    const MemoryGraph singleton = graphFromMask(1, 1);
    assertLabels(singleton, {0}, "singleton self-loop");
    ++cases;
  }

  constexpr uint32_t kDeepVertices = 200000;
  {
    MemoryGraph chain;
    chain.vertices = kDeepVertices;
    chain.rowEnds.resize(kDeepVertices);
    chain.destinations.reserve(kDeepVertices - 1);
    std::vector<Vertex> expected(kDeepVertices);
    for (Vertex vertex = 0; vertex < kDeepVertices; ++vertex) {
      if (vertex + 1 < kDeepVertices) {
        chain.destinations.push_back(vertex + 1);
      }
      chain.rowEnds[vertex] = chain.destinations.size();
      expected[vertex] = vertex;
    }
    assertLabels(chain, expected, "deep directed chain");
    ++cases;
  }
  {
    MemoryGraph cycle;
    cycle.vertices = kDeepVertices;
    cycle.rowEnds.resize(kDeepVertices);
    cycle.destinations.reserve(kDeepVertices);
    for (Vertex vertex = 0; vertex < kDeepVertices; ++vertex) {
      cycle.destinations.push_back((vertex + 1) % kDeepVertices);
      cycle.rowEnds[vertex] = cycle.destinations.size();
    }
    assertLabels(
        cycle, std::vector<Vertex>(kDeepVertices, 0), "deep directed cycle");
    ++cases;
  }

  std::cout << "SELF_TEST_OK cases=" << cases
            << " algorithms=tarjan,gabow,pearce,tarjan-zwick"
            << " oracle=transitive_closure"
            << " random_cases=2048"
            << " deep_vertices=" << kDeepVertices << '\n';
}

double parseLimit(const char* text) {
  size_t consumed = 0;
  const std::string value(text);
  const double limit = std::stod(value, &consumed);
  if (consumed != value.size() || limit < 0) {
    throw std::runtime_error(
        "SCC_SECONDS_LIMIT must be a non-negative number");
  }
  return limit;
}

void printUsage(const char* program) {
  std::cerr
      << "usage: " << program
      << " INPUT.bgr tarjan|gabow|pearce|tarjan-zwick"
      << " [LABELS.bin|-] [SCC_SECONDS_LIMIT]\n"
      << "       " << program << " --self-test\n";
}

}  // namespace

int main(int argc, char** argv) {
  const auto processStart = Clock::now();
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      runSelfTest();
      return 0;
    }
    if (argc < 3 || argc > 5) {
      printUsage(argv[0]);
      return 2;
    }

    const std::string input = argv[1];
    const std::string algorithm = argv[2];
    if (algorithm != "tarjan" && algorithm != "gabow" &&
        algorithm != "pearce" && algorithm != "tarjan-zwick") {
      throw std::runtime_error(
          "algorithm must be 'tarjan', 'gabow', 'pearce', or "
          "'tarjan-zwick'");
    }
    const std::string labelsPath = argc >= 4 ? argv[3] : "-";
    const double limitSeconds = argc == 5 ? parseLimit(argv[4]) : 0;

    const auto loadStart = Clock::now();
    MappedBgr graph(input);
    graph.validate();
    graph.assertUnchanged();
    const double loadSeconds = elapsedSeconds(loadStart);

    graph.prepareForScc();
    const auto sccStart = Clock::now();
    SccResult result = runAlgorithm(graph, algorithm, limitSeconds);
    const double sccSeconds = elapsedSeconds(sccStart);
    graph.assertUnchanged();

    const auto labelStart = Clock::now();
    writeLabels(
        labelsPath, result.labels.get(), graph.vertexCount());
    const double labelSeconds = elapsedSeconds(labelStart);

    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
      throw std::runtime_error("getrusage failed");
    }

    std::cout << std::fixed << std::setprecision(9)
              << "SCC_RESULT {"
              << "\"schema\":2,"
              << "\"algorithm\":" << jsonQuote(algorithm) << ","
              << "\"input\":" << jsonQuote(input) << ","
              << "\"labels\":" << jsonQuote(labelsPath) << ","
              << "\"nodes\":" << graph.vertexCount() << ","
              << "\"edges\":" << graph.edgeCount() << ","
              << "\"bgr_flags\":" << graph.flags() << ","
              << "\"weighted\":" << (graph.weighted() ? "true" : "false")
              << ",\"components\":" << result.components
              << ",\"largest\":" << result.largest
              << ",\"scanned_edges\":" << result.scannedEdges
              << ",\"load_seconds\":" << loadSeconds
              << ",\"scc_seconds\":" << sccSeconds
              << ",\"label_write_seconds\":" << labelSeconds
              << ",\"total_seconds\":" << elapsedSeconds(processStart)
              << ",\"max_rss_kib\":" << usage.ru_maxrss
              << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SCC_ERROR {\"error\":" << jsonQuote(error.what())
              << ",\"runtime_seconds\":" << elapsedSeconds(processStart)
              << "}\n";
    return 1;
  }
}
