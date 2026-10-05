#pragma once

#include "scc/algorithm.hpp"

namespace scc {

SccResult runGabow(const GraphView& graph, const RunOptions& options);

}  // namespace scc
