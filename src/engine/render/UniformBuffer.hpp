#pragma once

#include "gl_core.hpp"
#include <cstdint>

namespace voxel {

/// Wrapper around an OpenGL Uniform Buffer Object.
class UniformBuffer {
public:
  UniformBuffer(uint32_t binding, size_t byteSize);
  ~UniformBuffer();

  UniformBuffer(const UniformBuffer&) = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;
  UniformBuffer(UniformBuffer&&) noexcept;
  UniformBuffer& operator=(UniformBuffer&&) noexcept;

  void upload(const void* data, size_t size) const;
  [[nodiscard]] auto buffer() const -> uint32_t { return m_buffer; }
  [[nodiscard]] auto binding() const -> uint32_t { return m_binding; }

private:
  uint32_t m_buffer = 0;
  uint32_t m_binding;
  size_t m_byteSize;
};

} // namespace voxel
