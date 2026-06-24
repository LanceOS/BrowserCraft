#pragma once

#include "VertexArray.hpp"
#include "VertexBuffer.hpp"
#include <cstdint>

namespace voxel {

/// GPU mesh for one chunk. Owns VAO, VBO, and EBO.
class ChunkMesh {
public:
  ChunkMesh();
  ~ChunkMesh() = default;

  ChunkMesh(const ChunkMesh&) = delete;
  ChunkMesh& operator=(const ChunkMesh&) = delete;
  ChunkMesh(ChunkMesh&&) = default;
  ChunkMesh& operator=(ChunkMesh&&) = default;

  /// Upload vertex and index data. Sets up vertex attribute pointers.
  void upload(const float* vertexData, size_t vertexFloats,
              const uint32_t* indexData, size_t indexCount,
              int32_t vertexStrideFloats);

  /// Draw the mesh using glDrawElements.
  void draw() const;

  [[nodiscard]] auto indexCount() const -> size_t { return m_indexCount; }
  [[nodiscard]] auto valid() const -> bool { return m_indexCount > 0; }

  void dispose();

private:
  VertexArray m_vao;
  VertexBuffer m_vbo{GL_ARRAY_BUFFER};
  VertexBuffer m_ebo{GL_ELEMENT_ARRAY_BUFFER};
  size_t m_indexCount = 0;
  bool m_uploaded = false;
};

} // namespace voxel
