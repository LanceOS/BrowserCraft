#include "VertexArray.hpp"

namespace terrain {

VertexArray::VertexArray() {
  gl::GenVertexArrays(1, &m_vao);
}

VertexArray::~VertexArray() {
  if (m_vao) gl::DeleteVertexArrays(1, &m_vao);
}

VertexArray::VertexArray(VertexArray&& other) noexcept : m_vao(other.m_vao) {
  other.m_vao = 0;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
  if (this != &other) {
    if (m_vao) gl::DeleteVertexArrays(1, &m_vao);
    m_vao = other.m_vao;
    other.m_vao = 0;
  }
  return *this;
}

void VertexArray::bind() const {
  gl::BindVertexArray(m_vao);
}

} // namespace terrain
