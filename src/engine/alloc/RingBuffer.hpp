#pragma once

#include <vector>
#include <optional>
#include <cstddef>

namespace voxel {

/// Fixed-capacity ring buffer (FIFO queue).
template <typename T>
class RingBuffer {
public:
  explicit RingBuffer(size_t capacity)
    : m_values(capacity), m_capacity(capacity) {}

  /// Push a value. Returns false if full.
  auto push(T value) -> bool {
    if (m_size == m_capacity) return false;
    m_values[m_tail] = std::move(value);
    m_tail = (m_tail + 1) % m_capacity;
    ++m_size;
    return true;
  }

  /// Pop the oldest value. Returns nullopt if empty.
  auto shift() -> std::optional<T> {
    if (m_size == 0) return std::nullopt;
    auto value = std::move(m_values[m_head]);
    m_values[m_head].reset();
    m_head = (m_head + 1) % m_capacity;
    --m_size;
    return value;
  }

  [[nodiscard]] auto size() const -> size_t { return m_size; }
  [[nodiscard]] auto capacity() const -> size_t { return m_capacity; }
  [[nodiscard]] auto empty() const -> bool { return m_size == 0; }
  [[nodiscard]] auto full() const -> bool { return m_size == m_capacity; }

private:
  std::vector<std::optional<T>> m_values;
  size_t m_head = 0;
  size_t m_tail = 0;
  size_t m_size = 0;
  size_t m_capacity;
};

} // namespace voxel
