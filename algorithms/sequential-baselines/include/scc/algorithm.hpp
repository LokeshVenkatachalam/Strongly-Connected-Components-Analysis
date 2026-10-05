#pragma once

#include "scc/graph.hpp"

#include <array>
#include <memory>
#include <string_view>

namespace scc {

struct RunOptions {
  double timeLimitSeconds = 0;
  bool reportProgress = true;
};

struct SccResult {
  std::unique_ptr<Vertex[]> labels;
  std::uint64_t components = 0;
  std::uint64_t largest = 0;
  std::uint64_t scannedEdges = 0;
  std::uint64_t discoveredVertices = 0;
  std::uint64_t assignedVertices = 0;
};

using AlgorithmFunction =
    SccResult (*)(const GraphView&, const RunOptions&);

struct AlgorithmDescriptor {
  std::string_view name;
  AlgorithmFunction run;
  bool requiresTranspose;
};

const std::array<AlgorithmDescriptor, 4>& algorithmRegistry();
const AlgorithmDescriptor& findAlgorithm(std::string_view name);

}  // namespace scc
