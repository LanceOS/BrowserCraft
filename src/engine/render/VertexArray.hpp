#pragma once

#include "gl_core.hpp"
#include <cstdint>

namespace terrain {

/// Wrapper around an OpenGL Vertex Array Object.
class VertexArray {
public:
  VertexArray();
  ~VertexArray();

  VertexArray(const VertexArray&) = delete;
  VertexArray& operator=(const VertexArray&) = delete;
  VertexArray(VertexArray&&) noexcept;
  VertexArray& operator=(VertexArray&&) noexcept;

  void bind() const;
  [[nodiscard]] auto vao() const -> uint32_t { return m_vao; }

private:
  uint32_t m_vao = 0;
};

} // namespace terrain
