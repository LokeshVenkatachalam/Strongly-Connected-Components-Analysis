#pragma once

#include "scc/algorithm.hpp"

#include <string>

namespace scc {

std::string jsonQuote(const std::string& value);

void writeLabels(
    const std::string& output,
    const Vertex* labels,
    std::uint64_t vertexCount);

void printSccResult(
    const std::string& algorithm,
    const BgrGraph& graph,
    const std::string& labelsPath,
    const SccResult& result,
    double sharedLoadSeconds,
    double sccSeconds,
    double labelSeconds,
    double totalSeconds,
    long maxRssKiB);

}  // namespace scc
