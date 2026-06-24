#pragma once

#include <vector>
#include <atomic>
#include <cstdint>
#include <optional>

namespace voxel {

/// Lock-free SPSC (Single Producer, Single Consumer) completion queue.
/// The producer (worker thread) calls push(), the consumer (main thread) calls poll().
class WorkerCompletionQueue {
public:
  explicit WorkerCompletionQueue(int32_t capacity);

  /// Push a slot index (called by worker). Returns false if full.
  auto push(int32_t slotIndex) -> bool;

  /// Poll a completed slot index (called by main thread). Returns nullopt if empty.
  auto poll() -> std::optional<int32_t>;

  [[nodiscard]] auto capacity() const -> int32_t { return m_capacity; }

private:
  int32_t m_capacity;
  std::atomic<int32_t> m_writeIndex{0};
  std::atomic<int32_t> m_readIndex{0};
  std::vector<int32_t> m_data;
  // Padding to avoid false sharing
  char m_padding[64]{};
};

} // namespace voxel
