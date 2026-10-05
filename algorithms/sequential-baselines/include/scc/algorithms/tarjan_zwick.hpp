#pragma once

#include "scc/algorithm.hpp"

namespace scc {

SccResult runTarjanZwick(
    const GraphView& graph, const RunOptions& options);

}  // namespace scc
