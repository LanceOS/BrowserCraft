#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>

namespace voxel {

constexpr int32_t ENTITY_INDEX_MASK = 0x00FF'FFFF;
constexpr int32_t ENTITY_GEN_SHIFT = 24;

/// Manages entity allocation, destruction, and lifecycle.
/// Entity IDs pack a generation counter into the high 8 bits for ABA protection.
class EntityManager {
public:
  explicit EntityManager(int32_t capacity = 1 << 18);

  /// Allocate a new entity. Throws if capacity is exhausted.
  [[nodiscard]] auto allocate() -> int32_t;

  /// Destroy an entity. Safe to call on already-destroyed entities.
  void destroy(int32_t id);

  /// Check if an entity ID is still alive.
  [[nodiscard]] auto isAlive(int32_t id) const -> bool;

  /// Extract the index portion of an entity ID.
  [[nodiscard]] static auto indexOf(int32_t id) -> int32_t {
    return id & ENTITY_INDEX_MASK;
  }

  /// Number of currently alive entities.
  [[nodiscard]] auto count() const -> int32_t { return m_liveCount; }

  [[nodiscard]] auto capacity() const -> int32_t { return m_capacity; }

private:
  int32_t m_capacity;
  std::vector<uint8_t> m_generation;
  std::vector<int32_t> m_freeIndices;
  int32_t m_freeHead = 0;
  int32_t m_liveCount = 0;
};

} // namespace voxel
