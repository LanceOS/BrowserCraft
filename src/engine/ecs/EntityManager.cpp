#include "EntityManager.hpp"

namespace voxel {

EntityManager::EntityManager(int32_t capacity)
  : m_capacity(capacity), m_generation(capacity, 0), m_freeIndices(capacity) {
  for (int32_t i = capacity - 1; i >= 0; --i) {
    m_freeIndices[m_freeHead++] = i;
  }
}

auto EntityManager::allocate() -> int32_t {
  if (m_freeHead == 0) {
    throw std::runtime_error("EntityManager capacity exhausted");
  }
  int32_t idx = m_freeIndices[--m_freeHead];
  uint8_t gen = m_generation[idx];
  ++m_liveCount;
  return (static_cast<int32_t>(gen) << ENTITY_GEN_SHIFT) | idx;
}

void EntityManager::destroy(int32_t id) {
  int32_t idx = id & ENTITY_INDEX_MASK;
  uint8_t gen = static_cast<uint8_t>(id >> ENTITY_GEN_SHIFT);
  if (gen != m_generation[idx]) return; // already destroyed
  m_generation[idx] = (gen + 1) & 0xFF;
  m_freeIndices[m_freeHead++] = idx;
  --m_liveCount;
}

auto EntityManager::isAlive(int32_t id) const -> bool {
  int32_t idx = id & ENTITY_INDEX_MASK;
  return static_cast<uint8_t>(id >> ENTITY_GEN_SHIFT) == m_generation[idx];
}

} // namespace voxel
