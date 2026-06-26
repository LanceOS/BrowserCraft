#include "VertexBuffer.hpp"
#include <stdexcept>

namespace terrain {

VertexBuffer::VertexBuffer(uint32_t target) : m_target(target) {
  gl::GenBuffers(1, &m_buffer);
}

VertexBuffer::~VertexBuffer() {
  if (m_buffer) gl::DeleteBuffers(1, &m_buffer);
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept
  : m_buffer(other.m_buffer), m_target(other.m_target) {
  other.m_buffer = 0;
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept {
  if (this != &other) {
    if (m_buffer) gl::DeleteBuffers(1, &m_buffer);
    m_buffer = other.m_buffer;
    m_target = other.m_target;
    other.m_buffer = 0;
  }
  return *this;
}

void VertexBuffer::upload(const void* data, size_t size, uint32_t usage) const {
  gl::BindBuffer(m_target, m_buffer);
  gl::BufferData(m_target, static_cast<GLsizeiptr>(size), data, usage);
}

} // namespace terrain
