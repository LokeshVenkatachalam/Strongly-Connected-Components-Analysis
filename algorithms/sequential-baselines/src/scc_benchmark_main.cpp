#include "scc/algorithm.hpp"
#include "scc/graph.hpp"
#include "scc/io.hpp"
#include "scc/types.hpp"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Options {
  std::string input;
  std::vector<std::string> algorithms;
  std::string labelsDirectory;
  double timeLimitSeconds = 0;
  unsigned validationThreads = 1;
  bool reportProgress = true;
};

double parseLimit(const std::string& value) {
  std::size_t consumed = 0;
  const double limit = std::stod(value, &consumed);
  if (consumed != value.size() || limit < 0) {
    throw std::runtime_error(
        "--time-limit must be a non-negative number");
  }
  return limit;
}

unsigned parseThreads(const std::string& value) {
  std::size_t consumed = 0;
  const unsigned long threads = std::stoul(value, &consumed);
  if (consumed != value.size() || threads == 0 || threads > 256) {
    throw std::runtime_error(
        "--validation-threads must be in the range 1..256");
  }
  return static_cast<unsigned>(threads);
}

std::vector<std::string> splitAlgorithms(const std::string& value) {
  if (value == "all") {
    std::vector<std::string> names;
    for (const auto& algorithm : scc::algorithmRegistry()) {
      names.emplace_back(algorithm.name);
    }
    return names;
  }

  std::vector<std::string> names;
  std::size_t first = 0;
  while (first <= value.size()) {
    const std::size_t comma = value.find(',', first);
    const std::size_t last =
        comma == std::string::npos ? value.size() : comma;
    const std::string name = value.substr(first, last - first);
    if (name.empty()) {
      throw std::runtime_error(
          "--algorithms contains an empty name");
    }
    names.push_back(name);
    if (comma == std::string::npos) break;
    first = comma + 1;
  }
  return names;
}

void printAlgorithms() {
  for (const auto& algorithm : scc::algorithmRegistry()) {
    std::cout << algorithm.name << '\n';
  }
}

void printUsage(const char* program) {
  std::cerr
      << "usage: " << program
      << " INPUT.bgr [--algorithms LIST|all]"
      << " [--labels-dir DIR] [--time-limit SECONDS]"
      << " [--validation-threads N] [--no-progress]\n"
      << "       " << program << " --list-algorithms\n";
}

Options parseOptions(int argc, char** argv) {
  if (argc == 2 &&
      std::string(argv[1]) == "--list-algorithms") {
    printAlgorithms();
    std::exit(0);
  }
  if (argc < 2) {
    printUsage(argv[0]);
    throw std::runtime_error("missing input graph");
  }

  Options options;
  options.input = argv[1];
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    auto requireValue = [&]() -> std::string {
      if (++index >= argc) {
        throw std::runtime_error(
            "missing value for " + argument);
      }
      return argv[index];
    };

    if (argument == "--algorithms") {
      options.algorithms = splitAlgorithms(requireValue());
    } else if (argument == "--labels-dir") {
      options.labelsDirectory = requireValue();
    } else if (argument == "--time-limit") {
      options.timeLimitSeconds = parseLimit(requireValue());
    } else if (argument == "--validation-threads") {
      options.validationThreads = parseThreads(requireValue());
    } else if (argument == "--no-progress") {
      options.reportProgress = false;
    } else {
      throw std::runtime_error("unknown option: " + argument);
    }
  }

  if (options.algorithms.empty()) {
    options.algorithms = splitAlgorithms("all");
  }
  std::set<std::string> unique;
  for (const auto& name : options.algorithms) {
    (void)scc::findAlgorithm(name);
    if (!unique.insert(name).second) {
      throw std::runtime_error(
          "algorithm listed more than once: " + name);
    }
  }
  return options;
}

std::string labelsPath(
    const Options& options, const std::string& algorithm) {
  if (options.labelsDirectory.empty()) return "-";
  const fs::path directory(options.labelsDirectory);
  fs::create_directories(directory);
  const std::string graph =
      fs::path(options.input).stem().string();
  return (directory / (graph + "." + algorithm + ".labels.bin"))
      .string();
}

std::string jsonAlgorithmList(
    const std::vector<std::string>& algorithms) {
  std::string output = "[";
  for (std::size_t index = 0; index < algorithms.size(); ++index) {
    if (index) output += ',';
    output += scc::jsonQuote(algorithms[index]);
  }
  return output + ']';
}

}  // namespace

int main(int argc, char** argv) {
  const auto processStart = scc::Clock::now();
  try {
    const Options options = parseOptions(argc, argv);

    const auto loadStart = scc::Clock::now();
    scc::BgrGraph graph(options.input);
    graph.validate(options.validationThreads);
    graph.assertUnchanged();
    const double loadSeconds = scc::elapsedSeconds(loadStart);
    const scc::GraphView view = graph.view();

    std::cout << std::fixed << std::setprecision(9)
              << "BGR_LOAD {"
              << "\"schema\":1,"
              << "\"input\":" << scc::jsonQuote(graph.path()) << ","
              << "\"nodes\":" << view.vertexCount() << ","
              << "\"edges\":" << view.edgeCount() << ","
              << "\"mapped_bytes\":" << graph.mappedBytes() << ","
              << "\"weighted\":"
              << (graph.weighted() ? "true" : "false") << ","
              << "\"load_seconds\":" << loadSeconds << ","
              << "\"validation_threads\":"
              << options.validationThreads << ","
              << "\"algorithms\":"
              << jsonAlgorithmList(options.algorithms)
              << "}\n";

    graph.prepareForScc();
    double algorithmSeconds = 0;
    for (const std::string& name : options.algorithms) {
      const auto& algorithm = scc::findAlgorithm(name);
      const auto runStart = scc::Clock::now();
      const auto sccStart = scc::Clock::now();
      scc::SccResult result = algorithm.run(
          view,
          {options.timeLimitSeconds, options.reportProgress});
      const double sccSeconds = scc::elapsedSeconds(sccStart);
      algorithmSeconds += sccSeconds;
      graph.assertUnchanged();

      const std::string output = labelsPath(options, name);
      const auto labelStart = scc::Clock::now();
      scc::writeLabels(
          output, result.labels.get(), view.vertexCount());
      const double labelSeconds = scc::elapsedSeconds(labelStart);

      struct rusage usage {};
      if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        throw std::runtime_error("getrusage failed");
      }
      scc::printSccResult(
          name,
          graph,
          output,
          result,
          loadSeconds,
          sccSeconds,
          labelSeconds,
          scc::elapsedSeconds(runStart),
          usage.ru_maxrss);
    }

    std::cout << std::fixed << std::setprecision(9)
              << "BENCHMARK_RESULT {"
              << "\"schema\":1,"
              << "\"input\":" << scc::jsonQuote(graph.path()) << ","
              << "\"algorithms\":"
              << jsonAlgorithmList(options.algorithms) << ","
              << "\"shared_load_seconds\":" << loadSeconds << ","
              << "\"algorithm_seconds\":" << algorithmSeconds << ","
              << "\"total_seconds\":"
              << scc::elapsedSeconds(processStart)
              << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SCC_ERROR {\"error\":"
              << scc::jsonQuote(error.what())
              << ",\"runtime_seconds\":"
              << scc::elapsedSeconds(processStart) << "}\n";
    return 1;
  }
}
