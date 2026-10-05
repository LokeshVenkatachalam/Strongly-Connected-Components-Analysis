#include "scc/algorithm.hpp"
#include "scc/graph.hpp"
#include "scc/io.hpp"
#include "scc/types.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>

namespace {

double parseLimit(const char* text) {
  std::size_t consumed = 0;
  const std::string value(text);
  const double limit = std::stod(value, &consumed);
  if (consumed != value.size() || limit < 0) {
    throw std::runtime_error(
        "SCC_SECONDS_LIMIT must be a non-negative number");
  }
  return limit;
}

void printUsage(const char* program) {
  std::cerr
      << "usage: " << program
      << " INPUT.bgr tarjan|gabow|pearce|tarjan-zwick"
      << " [LABELS.bin|-] [SCC_SECONDS_LIMIT]\n";
}

}  // namespace

int main(int argc, char** argv) {
  const auto processStart = scc::Clock::now();
  try {
    if (argc < 3 || argc > 5) {
      printUsage(argv[0]);
      return 2;
    }

    const std::string input = argv[1];
    const std::string algorithmName = argv[2];
    const auto& algorithm = scc::findAlgorithm(algorithmName);
    const std::string labelsPath = argc >= 4 ? argv[3] : "-";
    const double limitSeconds =
        argc == 5 ? parseLimit(argv[4]) : 0;

    const auto loadStart = scc::Clock::now();
    scc::BgrGraph graph(input);
    graph.validate();
    graph.assertUnchanged();
    const double loadSeconds = scc::elapsedSeconds(loadStart);

    graph.prepareForScc();
    const auto sccStart = scc::Clock::now();
    scc::SccResult result =
        algorithm.run(graph.view(), {limitSeconds, true});
    const double sccSeconds = scc::elapsedSeconds(sccStart);
    graph.assertUnchanged();

    const auto labelStart = scc::Clock::now();
    scc::writeLabels(
        labelsPath, result.labels.get(), graph.view().vertexCount());
    const double labelSeconds = scc::elapsedSeconds(labelStart);

    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
      throw std::runtime_error("getrusage failed");
    }
    scc::printSccResult(
        algorithmName,
        graph,
        labelsPath,
        result,
        loadSeconds,
        sccSeconds,
        labelSeconds,
        scc::elapsedSeconds(processStart),
        usage.ru_maxrss);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SCC_ERROR {\"error\":"
              << scc::jsonQuote(error.what())
              << ",\"runtime_seconds\":"
              << scc::elapsedSeconds(processStart) << "}\n";
    return 1;
  }
}
