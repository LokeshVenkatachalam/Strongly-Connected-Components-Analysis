#pragma once

#include "scc/types.hpp"

#include <cstring>
#include <memory>
#include <string>

namespace scc {

template <class T>
inline T loadUnaligned(const std::uint8_t* address) {
  T value;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

class GraphView {
 public:
  GraphView() = default;

  GraphView(
      const std::uint8_t* rowData,
      const std::uint8_t* destinationData,
      std::uint64_t vertices,
      std::uint64_t edges,
      unsigned nodeBytes,
      unsigned edgeBytes)
      : rowData_(rowData),
        destinationData_(destinationData),
        vertices_(vertices),
        edges_(edges),
        nodeBytes_(nodeBytes),
        edgeBytes_(edgeBytes) {}

  std::uint64_t vertexCount() const { return vertices_; }
  std::uint64_t edgeCount() const { return edges_; }

  EdgeIndex edgeBegin(Vertex vertex) const {
    return vertex ? edgeEnd(vertex - 1) : 0;
  }

  EdgeIndex edgeEnd(Vertex vertex) const {
    const std::uint8_t* value =
        rowData_ + static_cast<std::uint64_t>(vertex) * edgeBytes_;
    return edgeBytes_ == 8 ? loadUnaligned<std::uint64_t>(value)
                           : loadUnaligned<std::uint32_t>(value);
  }

  Vertex destination(EdgeIndex edge) const {
    const std::uint8_t* value =
        destinationData_ + edge * nodeBytes_;
    const std::uint64_t destination =
        nodeBytes_ == 8 ? loadUnaligned<std::uint64_t>(value)
                        : loadUnaligned<std::uint32_t>(value);
    if (destination >= vertices_) {
      throw std::runtime_error(
          "BGR destination out of range at edge " +
          std::to_string(edge));
    }
    return static_cast<Vertex>(destination);
  }

 private:
  const std::uint8_t* rowData_ = nullptr;
  const std::uint8_t* destinationData_ = nullptr;
  std::uint64_t vertices_ = 0;
  std::uint64_t edges_ = 0;
  unsigned nodeBytes_ = 0;
  unsigned edgeBytes_ = 0;
};

class BgrGraph {
 public:
  explicit BgrGraph(std::string path);
  ~BgrGraph();

  BgrGraph(const BgrGraph&) = delete;
  BgrGraph& operator=(const BgrGraph&) = delete;
  BgrGraph(BgrGraph&&) noexcept;
  BgrGraph& operator=(BgrGraph&&) noexcept;

  GraphView view() const;
  void validate(unsigned threads = 1) const;
  void prepareForScc() const;
  void assertUnchanged() const;

  unsigned flags() const;
  bool weighted() const;
  const std::string& path() const;
  std::uint64_t mappedBytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace scc
