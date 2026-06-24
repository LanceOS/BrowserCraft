#include "LockFreeRingBuffer.hpp"

namespace voxel {

WorkerCompletionQueue::WorkerCompletionQueue(int32_t capacity)
  : m_capacity(capacity), m_data(capacity, 0) {}

auto WorkerCompletionQueue::push(int32_t slotIndex) -> bool {
  int32_t write = m_writeIndex.load(std::memory_order_relaxed);
  int32_t read = m_readIndex.load(std::memory_order_acquire);
  if (write - read >= m_capacity) return false;
  m_data[write % m_capacity] = slotIndex;
  m_writeIndex.store(write + 1, std::memory_order_release);
  return true;
}

auto WorkerCompletionQueue::poll() -> std::optional<int32_t> {
  int32_t read = m_readIndex.load(std::memory_order_relaxed);
  int32_t write = m_writeIndex.load(std::memory_order_acquire);
  if (read == write) return std::nullopt;
  int32_t slotIndex = m_data[read % m_capacity];
  m_readIndex.store(read + 1, std::memory_order_release);
  return slotIndex;
}

} // namespace voxel
