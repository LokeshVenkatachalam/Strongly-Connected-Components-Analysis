#include "scc/algorithms/tarjan.hpp"

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

SccResult runTarjan(
    const GraphView& graph, const RunOptions& options) {
  const auto start = Clock::now();
  const detail::Deadline deadline(options.timeLimitSeconds, start);
  const std::uint64_t vertexCount = graph.vertexCount();
  SccResult result = detail::createResult(vertexCount);
  if (vertexCount == 0) return result;

  std::unique_ptr<EdgeIndex[]> order(new EdgeIndex[vertexCount]);
  std::unique_ptr<EdgeIndex[]> low(new EdgeIndex[vertexCount]);
  std::unique_ptr<Vertex[]> active(new Vertex[vertexCount]);
  std::unique_ptr<Frame[]> dfs(new Frame[vertexCount]);
  std::fill_n(order.get(), vertexCount, kNoOrder);

  EdgeIndex nextOrder = 0;
  std::uint64_t activeSize = 0;
  std::uint64_t depth = 0;

  auto discover = [&](Vertex vertex) {
    order[vertex] = low[vertex] = nextOrder++;
    active[activeSize++] = vertex;
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
            "tarjan", result, start, options.reportProgress);
        if (order[destination] == kNoOrder) {
          discover(destination);
        } else if (result.labels[destination] == kNoVertex) {
          low[vertex] = std::min(low[vertex], order[destination]);
        }
        continue;
      }

      if (low[vertex] == order[vertex]) {
        const std::uint64_t oldActiveSize = activeSize;
        Vertex minimum = kNoVertex;
        Vertex member;
        do {
          if (activeSize == 0) {
            throw std::runtime_error(
                "Tarjan active-stack underflow");
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

}  // namespace scc
