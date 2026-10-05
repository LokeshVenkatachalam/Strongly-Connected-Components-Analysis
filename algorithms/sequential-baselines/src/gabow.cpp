#include "scc/algorithms/gabow.hpp"

#include "algorithm_support.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace scc {
namespace {

struct Frame {
  EdgeIndex nextEdge;
  Vertex vertex;
};

}  // namespace

SccResult runGabow(
    const GraphView& graph, const RunOptions& options) {
  const auto start = Clock::now();
  const detail::Deadline deadline(options.timeLimitSeconds, start);
  const std::uint64_t vertexCount = graph.vertexCount();
  SccResult result = detail::createResult(vertexCount);
  if (vertexCount == 0) return result;

  std::unique_ptr<EdgeIndex[]> order(new EdgeIndex[vertexCount]);
  std::unique_ptr<Vertex[]> active(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> roots(new Vertex[vertexCount]);
  std::unique_ptr<Frame[]> dfs(new Frame[vertexCount]);
  std::fill_n(order.get(), vertexCount, kNoOrder);

  EdgeIndex nextOrder = 0;
  std::uint64_t activeSize = 0;
  std::uint64_t rootsSize = 0;
  std::uint64_t depth = 0;

  auto discover = [&](Vertex vertex) {
    order[vertex] = nextOrder++;
    active[activeSize++] = vertex;
    roots[rootsSize++] = vertex;
    dfs[depth++] = Frame{graph.edgeBegin(vertex), vertex};
    result.discoveredVertices = nextOrder;
  };

  for (std::uint64_t root = 0; root < vertexCount; ++root) {
    if (order[root] != kNoOrder) continue;
    deadline.check(result.scannedEdges, true);
    discover(static_cast<Vertex>(root));

    while (depth) {
      Frame& frame = dfs[depth - 1];
      const Vertex vertex = frame.vertex;
      const EdgeIndex end = graph.edgeEnd(vertex);
      if (frame.nextEdge < end) {
        const Vertex destination =
            graph.destination(frame.nextEdge++);
        ++result.scannedEdges;
        deadline.check(result.scannedEdges);
        detail::reportProgress(
            "gabow", result, start, options.reportProgress);
        if (order[destination] == kNoOrder) {
          discover(destination);
        } else if (result.labels[destination] == kNoVertex) {
          while (rootsSize &&
                 order[roots[rootsSize - 1]] >
                     order[destination]) {
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
        const std::uint64_t oldActiveSize = activeSize;
        Vertex minimum = kNoVertex;
        Vertex member;
        do {
          if (activeSize == 0) {
            throw std::runtime_error(
                "Gabow active-stack underflow");
          }
          member = active[--activeSize];
          minimum = std::min(minimum, member);
        } while (member != vertex);

        for (std::uint64_t index = activeSize;
             index < oldActiveSize; ++index) {
          result.labels[active[index]] = minimum;
        }
        const std::uint64_t componentSize =
            oldActiveSize - activeSize;
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

}  // namespace scc
