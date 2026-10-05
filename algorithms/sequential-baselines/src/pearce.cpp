#include "scc/algorithms/pearce.hpp"

#include "algorithm_support.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

/*
 * Adapted from scipy/sparse/csgraph/_traversal.pyx in SciPy 1.17.0.
 * SciPy is distributed under the BSD 3-Clause license; see
 * ../SCIPY_LICENSE.txt.
 */
namespace scc {

SccResult runPearce(
    const GraphView& graph, const RunOptions& options) {
  const auto start = Clock::now();
  const detail::Deadline deadline(options.timeLimitSeconds, start);
  const std::uint64_t vertexCount = graph.vertexCount();
  constexpr Vertex kStackEnd = kNoVertex - 1;
  if (vertexCount >= kStackEnd) {
    throw std::runtime_error(
        "Pearce requires fewer than 2^32-2 vertices");
  }

  SccResult result = detail::createResult(vertexCount);
  if (vertexCount == 0) return result;

  std::unique_ptr<Vertex[]> lowLink(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> sharedLink(new Vertex[vertexCount]);
  std::unique_ptr<Vertex[]> stackBackward(new Vertex[vertexCount]);
  std::fill_n(lowLink.get(), vertexCount, kNoVertex);
  std::fill_n(sharedLink.get(), vertexCount, kNoVertex);
  std::fill_n(stackBackward.get(), vertexCount, kNoVertex);

  Vertex componentStackHead = kStackEnd;
  Vertex dfsStackHead = kStackEnd;
  std::uint64_t index = 0;

  for (std::uint64_t root = 0; root < vertexCount; ++root) {
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
          detail::reportProgress(
              "pearce", result, start, options.reportProgress);
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
        detail::reportProgress(
            "pearce", result, start, options.reportProgress);
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
      std::uint64_t emittedSize = 1;
      while (newHead != kStackEnd &&
             lowLink[vertex] <= lowLink[newHead]) {
        minimum = std::min(minimum, newHead);
        ++emittedSize;
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
      result.assignedVertices += emittedSize;
      result.largest = std::max(result.largest, emittedSize);
      ++result.components;
    }
  }

  if (result.discoveredVertices != vertexCount ||
      result.assignedVertices != vertexCount ||
      result.scannedEdges !=
          checkedMultiply(graph.edgeCount(), 2) ||
      componentStackHead != kStackEnd || index != 0) {
    throw std::runtime_error("Pearce coverage invariant failed");
  }
  return result;
}

}  // namespace scc
