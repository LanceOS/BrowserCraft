#pragma once

#include <cstdint>
#include <cstddef>

namespace terrain {

/// Lightweight struct defining a chunk's position inside a massive persistently mapped VBO/EBO.
class ChunkMesh {
public:
  ChunkMesh() = default;
  ~ChunkMesh() = default;

  void setup(size_t vboOffset, size_t iboOffset, int32_t baseVertex, size_t indexCount) {
    m_vboOffset = vboOffset;
    m_iboOffset = iboOffset;
    m_baseVertex = baseVertex;
    m_indexCount = indexCount;
  }

  [[nodiscard]] auto vboOffset() const -> size_t { return m_vboOffset; }
  [[nodiscard]] auto iboOffset() const -> size_t { return m_iboOffset; }
  [[nodiscard]] auto baseVertex() const -> int32_t { return m_baseVertex; }
  [[nodiscard]] auto indexCount() const -> size_t { return m_indexCount; }
  [[nodiscard]] auto valid() const -> bool { return m_indexCount > 0; }

  void dispose() {
    m_indexCount = 0;
  }

private:
  size_t m_vboOffset = 0;
  size_t m_iboOffset = 0;
  int32_t m_baseVertex = 0;
  size_t m_indexCount = 0;
};

} // namespace terrain
