#pragma once

#include <cstddef>
#include <cstdint>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

/// Interface for asynchronous chunk generation and meshing.
/// Implementations wire to thread pools or other execution backends.
class IChunkWorker {
public:
  virtual ~IChunkWorker() = default;

  /// Generate voxel data for a chunk. Called on a worker thread.
  virtual void generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed) = 0;

  /// Build mesh from voxel data. Called on a worker thread.
  /// The implementation should write vertex/index data directly to the
  /// persistently mapped GPU VBO/IBO at the computed slot offset.
  virtual void mesh(int32_t slotIndex) = 0;

  /// Set GPU buffer targets where the mesher writes vertex/index data directly.
  /// Called from the main thread; the pointers are read-only from worker threads
  /// (the mapped GPU memory itself is written from workers with MAP_COHERENT_BIT).
  virtual void setGpuTargets(float* vboPtr, size_t vboMaxBytes,
                             uint32_t* iboPtr, size_t iboMaxBytes) = 0;
};

} // namespace voxel
