#pragma once

#include "PersistentBuffer.hpp"
#include "ChunkMesh.hpp"
#include "../../engine/core/Config.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace terrain {

/// Compact allocator for chunk mesh GPU memory.
///
/// The arena owns a single persistently mapped VBO/IBO pair and suballocates
/// exact regions per chunk instead of reserving a full worst-case slot for
/// every possible chunk.
struct MeshAllocation {
  int32_t slotIndex = -1;
  size_t vboOffsetBytes = 0;
  size_t iboOffsetBytes = 0;
  size_t vboBytes = 0;
  size_t iboBytes = 0;
  int32_t baseVertex = 0;
  float* vboPtr = nullptr;
  uint32_t* iboPtr = nullptr;

  [[nodiscard]] auto valid() const -> bool {
    return slotIndex >= 0 && vboPtr != nullptr && iboPtr != nullptr;
  }
};

class ChunkMeshAllocator {
public:
  explicit ChunkMeshAllocator(const GameConfig& config, int32_t maxSlots);
  ~ChunkMeshAllocator() = default;

  ChunkMeshAllocator(const ChunkMeshAllocator&) = delete;
  ChunkMeshAllocator& operator=(const ChunkMeshAllocator&) = delete;

  [[nodiscard]] auto maxSlots() const -> int32_t { return m_maxSlots; }
  [[nodiscard]] auto vboBuffer() const -> uint32_t { return m_vbo.buffer(); }
  [[nodiscard]] auto iboBuffer() const -> uint32_t { return m_ibo.buffer(); }
  [[nodiscard]] auto vboPtr() -> float* { return static_cast<float*>(m_vbo.mappedPtr()); }
  [[nodiscard]] auto iboPtr() -> uint32_t* { return static_cast<uint32_t*>(m_ibo.mappedPtr()); }
  [[nodiscard]] auto vboCapacityBytes() const -> size_t { return m_vboCapacityBytes; }
  [[nodiscard]] auto iboCapacityBytes() const -> size_t { return m_iboCapacityBytes; }

  /// Reserve a region for a chunk slot. Replaces any existing allocation for
  /// the slot.
  [[nodiscard]] auto allocateForSlot(int32_t slotIndex,
                                     size_t vboBytes,
                                     size_t iboBytes) -> std::optional<MeshAllocation>;

  /// Release the allocation owned by a slot, if any.
  void releaseSlot(int32_t slotIndex);

  /// Look up the current allocation for a slot.
  [[nodiscard]] auto allocationForSlot(int32_t slotIndex) const -> std::optional<MeshAllocation>;

  /// Release allocations for slots that are no longer live.
  template <typename F>
  void releaseMissing(const std::vector<uint8_t>& liveMask, F&& onRelease) {
    std::vector<int32_t> staleSlots;
    {
      std::lock_guard lock(m_mutex);
      staleSlots.reserve(m_allocations.size());
      for (const auto& [slotIndex, alloc] : m_allocations) {
        if (slotIndex < 0 || slotIndex >= static_cast<int32_t>(liveMask.size()) ||
            liveMask[slotIndex] == 0u) {
          staleSlots.push_back(slotIndex);
        }
      }
    }

    for (int32_t slotIndex : staleSlots) {
      releaseSlot(slotIndex);
      onRelease(slotIndex);
    }
  }

private:
  struct FreeSpan {
    size_t offset = 0;
    size_t bytes = 0;
  };

  static auto alignUp(size_t value, size_t alignment) -> size_t;

  auto allocateSpan(std::map<size_t, size_t>& freeList,
                    size_t bytes,
                    size_t alignment) -> std::optional<size_t>;
  void freeSpan(std::map<size_t, size_t>& freeList, size_t offset, size_t bytes);
  [[nodiscard]] auto consumeSpan(std::map<size_t, size_t>& freeList,
                                 size_t offset,
                                 size_t bytes) -> bool;
  void releaseSlotLocked(int32_t slotIndex);

  [[nodiscard]] auto allocationLocked(int32_t slotIndex) const -> std::optional<MeshAllocation>;

  int32_t m_maxSlots = 0;
  size_t m_vertexBytes = 0;
  size_t m_vboCapacityBytes = 0;
  size_t m_iboCapacityBytes = 0;

  PersistentBuffer m_vbo;
  PersistentBuffer m_ibo;

  std::map<size_t, size_t> m_freeVbo;
  std::map<size_t, size_t> m_freeIbo;
  std::unordered_map<int32_t, MeshAllocation> m_allocations;
  mutable std::mutex m_mutex;
};

} // namespace terrain
