#include "scc/io.hpp"

#include "scc/types.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/types.h>
#include <unistd.h>

namespace scc {
namespace {

void writeExact(
    int fd,
    const void* data,
    std::size_t bytes,
    std::uint64_t offset) {
  const auto* source = static_cast<const std::uint8_t*>(data);
  std::size_t completed = 0;
  while (completed < bytes) {
    const ssize_t written = ::pwrite(
        fd,
        source + completed,
        bytes - completed,
        static_cast<off_t>(offset + completed));
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      throw std::runtime_error(
          "label write failed: " +
          std::string(std::strerror(errno)));
    }
    completed += static_cast<std::size_t>(written);
  }
}

}  // namespace

std::string jsonQuote(const std::string& value) {
  std::string output = "\"";
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20) {
          char escaped[7];
          std::snprintf(
              escaped,
              sizeof(escaped),
              "\\u%04x",
              static_cast<unsigned>(character));
          output += escaped;
        } else {
          output += static_cast<char>(character);
        }
    }
  }
  return output + '"';
}

void writeLabels(
    const std::string& output,
    const Vertex* labels,
    std::uint64_t vertexCount) {
  if (output == "-") return;
  const std::uint64_t bytes =
      checkedMultiply(vertexCount, sizeof(Vertex));
  if (bytes >
      static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
    throw std::runtime_error(
        "label output exceeds platform file limit");
  }

  const std::string partial =
      output + ".partial." + std::to_string(::getpid());
  int fd = ::open(
      partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error(
        "cannot create label output: " +
        std::string(std::strerror(errno)));
  }
  try {
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
      throw std::runtime_error("cannot size label output");
    }
    constexpr std::uint64_t kChunkBytes = 1ULL << 30;
    for (std::uint64_t offset = 0; offset < bytes;
         offset += kChunkBytes) {
      const std::uint64_t count =
          std::min(kChunkBytes, bytes - offset);
      writeExact(
          fd,
          reinterpret_cast<const std::uint8_t*>(labels) + offset,
          static_cast<std::size_t>(count),
          offset);
    }
    if (::fsync(fd) != 0) {
      throw std::runtime_error("cannot fsync label output");
    }
    if (::close(fd) != 0) {
      fd = -1;
      throw std::runtime_error("cannot close label output");
    }
    fd = -1;
    if (::rename(partial.c_str(), output.c_str()) != 0) {
      throw std::runtime_error(
          "cannot publish label output: " +
          std::string(std::strerror(errno)));
    }
  } catch (...) {
    if (fd >= 0) ::close(fd);
    ::unlink(partial.c_str());
    throw;
  }
}

void printSccResult(
    const std::string& algorithm,
    const BgrGraph& graph,
    const std::string& labelsPath,
    const SccResult& result,
    double sharedLoadSeconds,
    double sccSeconds,
    double labelSeconds,
    double totalSeconds,
    long maxRssKiB) {
  const GraphView view = graph.view();
  std::cout << std::fixed << std::setprecision(9)
            << "SCC_RESULT {"
            << "\"schema\":3,"
            << "\"algorithm\":" << jsonQuote(algorithm) << ","
            << "\"input\":" << jsonQuote(graph.path()) << ","
            << "\"labels\":" << jsonQuote(labelsPath) << ","
            << "\"nodes\":" << view.vertexCount() << ","
            << "\"edges\":" << view.edgeCount() << ","
            << "\"bgr_flags\":" << graph.flags() << ","
            << "\"weighted\":"
            << (graph.weighted() ? "true" : "false")
            << ",\"components\":" << result.components
            << ",\"largest\":" << result.largest
            << ",\"scanned_edges\":" << result.scannedEdges
            << ",\"shared_load_seconds\":" << sharedLoadSeconds
            << ",\"scc_seconds\":" << sccSeconds
            << ",\"label_write_seconds\":" << labelSeconds
            << ",\"total_seconds\":" << totalSeconds
            << ",\"max_rss_kib\":" << maxRssKiB
            << "}\n";
}

}  // namespace scc
