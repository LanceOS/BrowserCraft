#pragma once

#include <cstdint>

namespace voxel {

/// Interface for asynchronous chunk generation and meshing.
/// Implementations wire to thread pools or other execution backends.
class IChunkWorker {
public:
  virtual ~IChunkWorker() = default;

  /// Generate voxel data for a chunk. Called on a worker thread.
  virtual void generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed) = 0;

  /// Build mesh from voxel data. Called on a worker thread.
  virtual void mesh(int32_t slotIndex) = 0;
};

} // namespace voxel
