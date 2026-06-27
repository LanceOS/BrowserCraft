#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

namespace terrain {

/// Linear bump allocator backed by a byte buffer.
/// All allocations are aligned to 4 bytes. Call reset() to free all at once.
class ScratchArena {
public:
  explicit ScratchArena(size_t byteSize = 1 << 20)
    : m_buffer(byteSize, 0), m_byteSize(byteSize) {}

  /// Allocate `count` elements of type T. T must be trivially constructible.
  template <typename T>
  auto alloc(size_t count) -> T* {
    static_assert(std::is_trivially_constructible_v<T>,
                  "ScratchArena only supports trivially constructible types");
    size_t bytes = count * sizeof(T);
    // Align to 4 bytes
    m_offset = (m_offset + 3) & ~size_t{3};
    if (m_offset + bytes > m_byteSize) {
      throw std::runtime_error("ScratchArena overflow");
    }
    auto* ptr = reinterpret_cast<T*>(m_buffer.data() + m_offset);
    m_offset += bytes;
    // Zero-initialize
    std::memset(ptr, 0, bytes);
    return ptr;
  }

  /// Reset the arena (all previous allocations are invalidated).
  void reset() { m_offset = 0; }

  [[nodiscard]] auto used() const -> size_t { return m_offset; }
  [[nodiscard]] auto capacity() const -> size_t { return m_byteSize; }

private:
  std::vector<uint8_t> m_buffer;
  size_t m_byteSize;
  size_t m_offset = 0;
};

} // namespace terrain
