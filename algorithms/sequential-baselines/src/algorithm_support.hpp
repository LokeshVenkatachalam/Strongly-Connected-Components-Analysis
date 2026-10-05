#pragma once

#include "scc/algorithm.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace scc::detail {

inline constexpr std::uint64_t kProgressInterval = 1ULL << 28;
inline constexpr std::uint64_t kDeadlineCheckInterval = 1ULL << 20;

class Deadline {
 public:
  Deadline(double seconds, Clock::time_point start)
      : seconds_(seconds), start_(start) {}

  void check(std::uint64_t edgesVisited, bool force = false) const {
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

inline void reportProgress(
    std::string_view algorithm,
    const SccResult& result,
    Clock::time_point start,
    bool enabled) {
  if (!enabled || result.scannedEdges == 0 ||
      (result.scannedEdges & (kProgressInterval - 1)) != 0) {
    return;
  }
  std::cerr << "SCC_PROGRESS {"
            << "\"algorithm\":\"" << algorithm << "\","
            << "\"seconds\":" << elapsedSeconds(start) << ","
            << "\"scanned_edges\":" << result.scannedEdges << ","
            << "\"discovered_vertices\":" << result.discoveredVertices
            << ",\"assigned_vertices\":" << result.assignedVertices
            << ",\"components\":" << result.components
            << "}\n";
}

inline SccResult createResult(std::uint64_t vertexCount) {
  SccResult result;
  result.labels.reset(new Vertex[vertexCount]);
  std::fill_n(result.labels.get(), vertexCount, kNoVertex);
  return result;
}

}  // namespace scc::detail
