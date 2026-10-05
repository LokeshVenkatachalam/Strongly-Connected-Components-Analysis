#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace scc {

using Clock = std::chrono::steady_clock;
using EdgeIndex = std::uint64_t;
using Vertex = std::uint32_t;

inline constexpr EdgeIndex kNoOrder =
    std::numeric_limits<EdgeIndex>::max();
inline constexpr Vertex kNoVertex =
    std::numeric_limits<Vertex>::max();

static_assert(sizeof(std::size_t) == 8, "a 64-bit platform is required");
static_assert(
    sizeof(EdgeIndex) == 8 &&
        std::numeric_limits<EdgeIndex>::max() >
            std::uint64_t{91792261600ULL},
    "edge indices must represent graphs larger than 2^32 edges");

inline double elapsedSeconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

inline std::uint64_t checkedAdd(
    std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    throw std::overflow_error("64-bit addition overflow");
  }
  return left + right;
}

inline std::uint64_t checkedMultiply(
    std::uint64_t left, std::uint64_t right) {
  if (right &&
      left > std::numeric_limits<std::uint64_t>::max() / right) {
    throw std::overflow_error("64-bit multiplication overflow");
  }
  return left * right;
}

}  // namespace scc
