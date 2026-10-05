#include "scc/algorithms/tarjan_zwick.hpp"

#include "algorithm_support.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

/*
 * Adapted from scipy/sparse/csgraph/_traversal.pyx at SciPy commit
 * 8b4175a6cbba602362234f90fe36bee4800d36b7. SciPy is distributed
 * under the BSD 3-Clause license; see ../SCIPY_LICENSE.txt.
 */
namespace scc {

SccResult runTarjanZwick(
    const GraphView& graph, const RunOptions& options) {
  const auto start = Clock::now();
  const detail::Deadline deadline(options.timeLimitSeconds, start);
  const std::uint64_t vertexCount = graph.vertexCount();
  constexpr Vertex kNonLead = Vertex{1} << 31;
  constexpr Vertex kNodeMask = kNonLead - 1;
  if (vertexCount > kNodeMask) {
    throw std::runtime_error(
        "Tarjan-Zwick requires at most 2^31-1 vertices");
  }

  SccResult result = detail::createResult(vertexCount);
  if (vertexCount == 0) return result;

  constexpr EdgeIndex kUnvisited =
      std::numeric_limits<EdgeIndex>::max();
  std::unique_ptr<EdgeIndex[]> successorPosition(
      new EdgeIndex[vertexCount]);
  std::unique_ptr<Vertex[]> highLink(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> stack(new Vertex[vertexCount]);
  std::fill_n(successorPosition.get(), vertexCount, kUnvisited);

  std::uint64_t dfsSize = 0;
  std::uint64_t pendingSize = 0;
  std::uint64_t index = vertexCount;
  bool done = false;

  auto decode = [&](Vertex encoded) { return encoded & kNodeMask; };
  auto pushDfs = [&](Vertex encoded) {
    if (dfsSize + pendingSize >= vertexCount) {
      throw std::runtime_error("Tarjan-Zwick stack overflow");
    }
    stack[dfsSize++] = encoded;
  };
  auto pushPending = [&](Vertex vertex) {
    if (dfsSize + pendingSize >= vertexCount) {
      throw std::runtime_error("Tarjan-Zwick stack overflow");
    }
    stack[vertexCount - ++pendingSize] = vertex;
  };
  auto pendingTop = [&]() {
    return stack[vertexCount - pendingSize];
  };

  for (std::uint64_t root = 0; root < vertexCount; ++root) {
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
        detail::reportProgress(
            "tarjan-zwick",
            result,
            start,
            options.reportProgress);

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
            std::fill_n(
                result.labels.get(), vertexCount, Vertex{0});
            result.components = 1;
            result.largest = vertexCount;
            result.assignedVertices = vertexCount;
            dfsSize = 0;
            pendingSize = 0;
            done = true;
            break;
          }
        }
        continue;
      }

      const bool isLead =
          (stack[dfsSize - 1] & kNonLead) == 0;
      if (isLead) {
        --dfsSize;
        const std::uint64_t oldPendingSize = pendingSize;
        Vertex minimum = vertex;
        while (pendingSize) {
          const Vertex top = pendingTop();
          if (highLink[vertex] < highLink[top]) break;
          minimum = std::min(minimum, top);
          --pendingSize;
          ++index;
        }

        const std::uint64_t first =
            vertexCount - oldPendingSize;
        const std::uint64_t last = vertexCount - pendingSize;
        for (std::uint64_t position = first;
             position < last; ++position) {
          result.labels[stack[position]] = minimum;
        }
        result.labels[vertex] = minimum;
        ++index;

        const std::uint64_t emittedSize =
            1 + oldPendingSize - pendingSize;
        result.assignedVertices += emittedSize;
        result.largest = std::max(result.largest, emittedSize);
        ++result.components;
      } else {
        --dfsSize;
        pushPending(vertex);
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
      pendingSize != 0 || result.scannedEdges > graph.edgeCount()) {
    throw std::runtime_error(
        "Tarjan-Zwick coverage invariant failed");
  }
  return result;
}

}  // namespace scc
