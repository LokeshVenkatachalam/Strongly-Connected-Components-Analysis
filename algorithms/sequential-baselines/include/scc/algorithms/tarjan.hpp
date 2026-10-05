#pragma once

#include "scc/algorithm.hpp"

namespace scc {

SccResult runTarjan(const GraphView& graph, const RunOptions& options);

}  // namespace scc
