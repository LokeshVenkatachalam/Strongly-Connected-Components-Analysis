#pragma once

#include "scc/algorithm.hpp"

namespace scc {

SccResult runPearce(const GraphView& graph, const RunOptions& options);

}  // namespace scc
