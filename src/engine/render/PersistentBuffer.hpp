#pragma once

#include "gl_core.hpp"
#include <cstdint>
#include <cstddef>
#include <stdexcept>

namespace terrain {

/// A persistently mapped buffer for zero-overhead GPU uploads.
class PersistentBuffer {
public:
  PersistentBuffer() = default;
  PersistentBuffer(size_t capacityBytes, uint32_t target) : m_capacity(capacityBytes), m_target(target) {
    gl::CreateBuffers(1, &m_buffer);
    
    uint32_t flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    gl::NamedBufferStorage(m_buffer, capacityBytes, nullptr, flags);
    
    m_mappedPtr = gl::MapNamedBufferRange(m_buffer, 0, capacityBytes, flags);
    if (!m_mappedPtr) {
        throw std::runtime_error("Failed to map persistent buffer");
    }
  }

  ~PersistentBuffer() {
    dispose();
  }

  PersistentBuffer(const PersistentBuffer&) = delete;
  PersistentBuffer& operator=(const PersistentBuffer&) = delete;

  PersistentBuffer(PersistentBuffer&& other) noexcept 
    : m_buffer(other.m_buffer), m_mappedPtr(other.m_mappedPtr), m_capacity(other.m_capacity), m_target(other.m_target) {
    other.m_buffer = 0;
    other.m_mappedPtr = nullptr;
    other.m_capacity = 0;
  }

  PersistentBuffer& operator=(PersistentBuffer&& other) noexcept {
    if (this != &other) {
        dispose();
        m_buffer = other.m_buffer;
        m_mappedPtr = other.m_mappedPtr;
        m_capacity = other.m_capacity;
        m_target = other.m_target;
        other.m_buffer = 0;
        other.m_mappedPtr = nullptr;
        other.m_capacity = 0;
    }
    return *this;
  }

  void dispose() {
    if (m_buffer) {
        gl::UnmapNamedBuffer(m_buffer);
        gl::DeleteBuffers(1, &m_buffer);
        m_buffer = 0;
        m_mappedPtr = nullptr;
        m_capacity = 0;
    }
  }

  [[nodiscard]] auto buffer() const -> uint32_t { return m_buffer; }
  [[nodiscard]] auto mappedPtr() const -> void* { return m_mappedPtr; }
  [[nodiscard]] auto capacity() const -> size_t { return m_capacity; }

private:
  uint32_t m_buffer = 0;
  void* m_mappedPtr = nullptr;
  size_t m_capacity = 0;
  uint32_t m_target = 0;
};

} // namespace terrain
