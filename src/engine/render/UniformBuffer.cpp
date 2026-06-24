#include "UniformBuffer.hpp"

namespace voxel {

UniformBuffer::UniformBuffer(uint32_t binding, size_t byteSize)
  : m_binding(binding), m_byteSize(byteSize) {
  gl::GenBuffers(1, &m_buffer);
  gl::BindBuffer(GL_UNIFORM_BUFFER, m_buffer);
  gl::BufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(byteSize), nullptr, GL_DYNAMIC_DRAW);
  gl::BindBufferBase(GL_UNIFORM_BUFFER, binding, m_buffer);
}

UniformBuffer::~UniformBuffer() {
  if (m_buffer) gl::DeleteBuffers(1, &m_buffer);
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept
  : m_buffer(other.m_buffer), m_binding(other.m_binding), m_byteSize(other.m_byteSize) {
  other.m_buffer = 0;
}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept {
  if (this != &other) {
    if (m_buffer) gl::DeleteBuffers(1, &m_buffer);
    m_buffer = other.m_buffer;
    m_binding = other.m_binding;
    m_byteSize = other.m_byteSize;
    other.m_buffer = 0;
  }
  return *this;
}

void UniformBuffer::upload(const void* data, size_t size) const {
  gl::BindBuffer(GL_UNIFORM_BUFFER, m_buffer);
  gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
  gl::BindBufferBase(GL_UNIFORM_BUFFER, m_binding, m_buffer);
}

} // namespace voxel
