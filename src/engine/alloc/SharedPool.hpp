#pragma once

#include <vector>
#include <cstdint>
#include <mutex>
#include <optional>
#include <memory>

namespace voxel {

enum class ChunkSlotStatus : int32_t {
  FREE = 0,
  GENERATING = 1,
  VOXELS_READY = 2,
  MESHING = 3,
  MESH_READY = 4,
  GPU_UPLOADED = 5,
};

struct ChunkDimensions {
  int32_t sizeX;
  int32_t sizeY;
  int32_t sizeZ;
  int32_t maxVertsPerChunk;
  int32_t maxIndicesPerChunk;
  int32_t vertexStrideFloats;
};

/// A view into one slot of the shared pool buffer.
/// All pointers point into the shared buffer — no ownership.
struct ChunkSlot {
  int32_t slotIndex;
  uint8_t* buffer;       // base of the entire pool buffer
  size_t baseByteOffset; // offset to this slot
  int32_t* status;       // ChunkSlotStatus
  uint32_t* vertexCount;
  uint32_t* indexCount;
  int32_t* chunkX;
  int32_t* chunkZ;
  uint32_t* genSeed;
  uint8_t* voxels;
  uint8_t* light;
  uint8_t* redstone;
  float* vertices;
  uint32_t* indices;
};

/// Shared memory pool for chunk data, accessible from multiple threads.
/// - acquire() / release(): main thread only (protected by mutex)
/// - view(): any thread (returns pointers into the shared buffer)
class SharedPool {
public:
  /// Create a new pool (allocates the buffer).
  static auto create(int32_t capacity, ChunkDimensions dims) -> std::unique_ptr<SharedPool>;

  /// Acquire a free slot. Main thread only. Returns nullopt if none free.
  auto acquire() -> std::optional<ChunkSlot>;

  /// Release a slot back to the free list. Main thread only.
  void release(ChunkSlot slot);

  /// Grow the pool to accommodate more slots. Existing slots keep their
  /// indices and data. Does nothing if newCapacity <= current capacity.
  void resize(int32_t newCapacity);

  /// Get a view of a slot by index. Thread-safe for reads/writes to
  /// different slots (each slot is a disjoint memory region).
  [[nodiscard]] auto view(int32_t slotIndex) const -> ChunkSlot;

  [[nodiscard]] auto capacity() const -> int32_t { return m_capacity; }
  [[nodiscard]] auto dimensions() const -> ChunkDimensions { return m_dims; }
  [[nodiscard]] auto slotByteSize() const -> size_t { return m_slotByteSize; }
  [[nodiscard]] auto buffer() -> uint8_t* { return m_buffer.data(); }
  [[nodiscard]] auto bufferSize() const -> size_t { return m_buffer.size(); }

private:
  SharedPool(int32_t capacity, ChunkDimensions dims);

  void computeLayout();

  int32_t m_capacity;
  ChunkDimensions m_dims;

  size_t m_headerBytes = 32;
  size_t m_voxelsBytes = 0;
  size_t m_lightBytes = 0;
  size_t m_redstoneBytes = 0;
  size_t m_vertsBytes = 0;
  size_t m_indicesBytes = 0;
  size_t m_slotByteSize = 0;

  std::vector<uint8_t> m_buffer;

  std::mutex m_mutex;
  std::vector<int32_t> m_freeList;
  int32_t m_freeHead = 0;
};

} // namespace voxel
