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
/// In the TS version this used SharedArrayBuffer; in C++ native we use a
/// mutex-protected std::vector<uint8_t>.
class SharedPool {
public:
  /// Create a new pool (owner side, allocates the buffer).
  static auto create(int32_t capacity, ChunkDimensions dims) -> std::unique_ptr<SharedPool>;

  /// Attach to an existing buffer (worker side).
  static auto attach(uint8_t* buffer, size_t bufferSize, int32_t capacity, ChunkDimensions dims)
    -> std::unique_ptr<SharedPool>;

  /// Acquire a free slot (owner side only). Returns nullopt if none free.
  auto acquire() -> std::optional<ChunkSlot>;

  /// Release a slot back to the free list (owner side only).
  void release(ChunkSlot slot);

  /// Get a view of a slot by index (both sides).
  auto view(int32_t slotIndex) -> ChunkSlot;

  [[nodiscard]] auto capacity() const -> int32_t { return m_capacity; }
  [[nodiscard]] auto dimensions() const -> ChunkDimensions { return m_dims; }
  [[nodiscard]] auto slotByteSize() const -> size_t { return m_slotByteSize; }
  [[nodiscard]] auto buffer() -> uint8_t* { return m_ownedBuffer.data(); }
  [[nodiscard]] auto bufferSize() const -> size_t { return m_ownedBuffer.size(); }

private:
  SharedPool(int32_t capacity, ChunkDimensions dims, bool owner);

  void computeLayout();

  int32_t m_capacity;
  ChunkDimensions m_dims;
  bool m_owner; // true = main thread (can acquire/release), false = worker

  size_t m_headerBytes = 32;
  size_t m_voxelsBytes = 0;
  size_t m_lightBytes = 0;
  size_t m_redstoneBytes = 0;
  size_t m_vertsBytes = 0;
  size_t m_indicesBytes = 0;
  size_t m_slotByteSize = 0;

  std::vector<uint8_t> m_ownedBuffer;
  uint8_t* m_externalBuffer = nullptr;
  size_t m_externalBufferSize = 0;

  std::mutex m_mutex;
  std::vector<int32_t> m_freeList;
  int32_t m_freeHead = 0;
};

} // namespace voxel
