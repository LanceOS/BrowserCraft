#pragma once

#include "gl_core.hpp"
#include <cstdint>

namespace voxel {

/// Wrapper around an OpenGL buffer object (VBO or EBO).
class VertexBuffer {
public:
  VertexBuffer(uint32_t target);
  ~VertexBuffer();

  VertexBuffer(const VertexBuffer&) = delete;
  VertexBuffer& operator=(const VertexBuffer&) = delete;
  VertexBuffer(VertexBuffer&&) noexcept;
  VertexBuffer& operator=(VertexBuffer&&) noexcept;

  void upload(const void* data, size_t size, uint32_t usage) const;
  [[nodiscard]] auto buffer() const -> uint32_t { return m_buffer; }

private:
  uint32_t m_buffer = 0;
  uint32_t m_target;
};

} // namespace voxel
