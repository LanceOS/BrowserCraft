#include "ChunkMesh.hpp"

namespace voxel {

ChunkMesh::ChunkMesh() = default;

void ChunkMesh::upload(const float* vertexData, size_t vertexFloats,
                       const uint32_t* indexData, size_t indexCount,
                       int32_t vertexStrideFloats) {
  size_t strideBytes = static_cast<size_t>(vertexStrideFloats) * sizeof(float);

  m_vao.bind();
  m_vbo.upload(vertexData, vertexFloats * sizeof(float), GL_STATIC_DRAW);
  m_ebo.upload(indexData, indexCount * sizeof(uint32_t), GL_STATIC_DRAW);

  // Layout: pos(3f), normal(3f), uv(2f), texLayer(1f), lightData(1f) = 10 floats = 40 bytes
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes), reinterpret_cast<void*>(0));
  gl::EnableVertexAttribArray(1);
  gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes), reinterpret_cast<void*>(12));
  gl::EnableVertexAttribArray(2);
  gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes), reinterpret_cast<void*>(24));
  gl::EnableVertexAttribArray(3);
  gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes), reinterpret_cast<void*>(32));
  gl::EnableVertexAttribArray(4);
  gl::VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(strideBytes), reinterpret_cast<void*>(36));

  m_indexCount = indexCount;
  m_uploaded = true;
}

void ChunkMesh::draw() const {
  m_vao.bind();
  gl::DrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
}

void ChunkMesh::dispose() {
  // RAII handles cleanup via destructors
  m_indexCount = 0;
  m_uploaded = false;
}

} // namespace voxel
