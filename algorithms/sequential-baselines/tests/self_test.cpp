#include "scc/algorithm.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct MemoryGraph {
  std::uint64_t vertices = 0;
  std::vector<scc::EdgeIndex> rowEnds;
  std::vector<scc::Vertex> destinations;

  scc::GraphView view() const {
    return scc::GraphView(
        reinterpret_cast<const std::uint8_t*>(rowEnds.data()),
        reinterpret_cast<const std::uint8_t*>(
            destinations.data()),
        vertices,
        destinations.size(),
        sizeof(scc::Vertex),
        sizeof(scc::EdgeIndex));
  }
};

MemoryGraph graphFromMask(unsigned vertices, std::uint64_t mask) {
  MemoryGraph graph;
  graph.vertices = vertices;
  graph.rowEnds.resize(vertices);
  for (unsigned source = 0; source < vertices; ++source) {
    for (unsigned destination = 0; destination < vertices;
         ++destination) {
      const unsigned bit = source * vertices + destination;
      if ((mask >> bit) & 1ULL) {
        graph.destinations.push_back(destination);
      }
    }
    graph.rowEnds[source] = graph.destinations.size();
  }
  return graph;
}

MemoryGraph randomGraph(unsigned vertices, std::uint64_t& state) {
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
    for (unsigned destination = 0; destination < vertices;
         ++destination) {
      if ((next() & 3U) == 0) {
        graph.destinations.push_back(destination);
      }
    }
    graph.rowEnds[source] = graph.destinations.size();
  }
  return graph;
}

std::vector<scc::Vertex> referenceLabels(
    const MemoryGraph& graph) {
  const std::size_t vertices = graph.vertices;
  std::vector<std::uint8_t> reachable(vertices * vertices, 0);
  const scc::GraphView view = graph.view();
  for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
    reachable[vertex * vertices + vertex] = 1;
    for (scc::EdgeIndex edge =
             view.edgeBegin(static_cast<scc::Vertex>(vertex));
         edge < view.edgeEnd(static_cast<scc::Vertex>(vertex));
         ++edge) {
      reachable[vertex * vertices + view.destination(edge)] = 1;
    }
  }
  for (std::size_t middle = 0; middle < vertices; ++middle) {
    for (std::size_t source = 0; source < vertices; ++source) {
      if (!reachable[source * vertices + middle]) continue;
      for (std::size_t destination = 0;
           destination < vertices; ++destination) {
        reachable[source * vertices + destination] |=
            reachable[middle * vertices + destination];
      }
    }
  }

  std::vector<scc::Vertex> labels(vertices, scc::kNoVertex);
  for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
    scc::Vertex minimum = static_cast<scc::Vertex>(vertex);
    for (std::size_t candidate = 0;
         candidate < vertices; ++candidate) {
      if (reachable[vertex * vertices + candidate] &&
          reachable[candidate * vertices + vertex]) {
        minimum = std::min(
            minimum, static_cast<scc::Vertex>(candidate));
      }
    }
    labels[vertex] = minimum;
  }
  return labels;
}

void assertLabels(
    const MemoryGraph& graph,
    const std::vector<scc::Vertex>& expected,
    const char* context) {
  for (const auto& algorithm : scc::algorithmRegistry()) {
    scc::SccResult actual =
        algorithm.run(graph.view(), {0, false});
    for (std::size_t vertex = 0; vertex < expected.size();
         ++vertex) {
      if (actual.labels[vertex] != expected[vertex]) {
        throw std::runtime_error(
            std::string(context) + ": " +
            std::string(algorithm.name) +
            " disagrees at vertex " + std::to_string(vertex));
      }
    }
  }
}

}  // namespace

int main() {
  std::uint64_t cases = 0;
  for (std::uint64_t mask = 0; mask < (1ULL << 16); ++mask) {
    const MemoryGraph graph = graphFromMask(4, mask);
    assertLabels(
        graph, referenceLabels(graph), "four-vertex oracle");
    ++cases;
  }

  std::uint64_t randomState = 0x6a09e667f3bcc909ULL;
  for (std::uint64_t test = 0; test < 2048; ++test) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    const unsigned vertices = 5 + randomState % 12;
    const MemoryGraph graph = randomGraph(vertices, randomState);
    assertLabels(
        graph, referenceLabels(graph), "random graph oracle");
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

  constexpr std::uint32_t kDeepVertices = 200000;
  {
    MemoryGraph chain;
    chain.vertices = kDeepVertices;
    chain.rowEnds.resize(kDeepVertices);
    chain.destinations.reserve(kDeepVertices - 1);
    std::vector<scc::Vertex> expected(kDeepVertices);
    for (scc::Vertex vertex = 0; vertex < kDeepVertices;
         ++vertex) {
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
    for (scc::Vertex vertex = 0; vertex < kDeepVertices;
         ++vertex) {
      cycle.destinations.push_back(
          (vertex + 1) % kDeepVertices);
      cycle.rowEnds[vertex] = cycle.destinations.size();
    }
    assertLabels(
        cycle,
        std::vector<scc::Vertex>(kDeepVertices, 0),
        "deep directed cycle");
    ++cases;
  }

  std::cout
      << "SELF_TEST_OK cases=" << cases
      << " algorithms=tarjan,gabow,pearce,tarjan-zwick"
      << " oracle=transitive_closure"
      << " random_cases=2048"
      << " deep_vertices=" << kDeepVertices << '\n';
}
