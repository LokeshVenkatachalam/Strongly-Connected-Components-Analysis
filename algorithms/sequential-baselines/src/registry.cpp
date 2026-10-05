#include "scc/algorithm.hpp"

#include "scc/algorithms/gabow.hpp"
#include "scc/algorithms/pearce.hpp"
#include "scc/algorithms/tarjan.hpp"
#include "scc/algorithms/tarjan_zwick.hpp"

#include <stdexcept>
#include <string>

namespace scc {

const std::array<AlgorithmDescriptor, 4>& algorithmRegistry() {
  static const std::array<AlgorithmDescriptor, 4> algorithms{{
      {"tarjan", &runTarjan, false},
      {"gabow", &runGabow, false},
      {"pearce", &runPearce, false},
      {"tarjan-zwick", &runTarjanZwick, false},
  }};
  return algorithms;
}

const AlgorithmDescriptor& findAlgorithm(std::string_view name) {
  for (const auto& algorithm : algorithmRegistry()) {
    if (algorithm.name == name) return algorithm;
  }
  throw std::runtime_error(
      "unknown algorithm '" + std::string(name) +
      "'; expected tarjan, gabow, pearce, or tarjan-zwick");
}

}  // namespace scc
