#pragma once

#include <cstdint>

// @deprecated Legacy terrain-world code retained during the render-only migration to triangle meshes.
namespace terrain {

struct TerrainBrush;

/// Interface for chunk save/load operations.
/// Implementations wire to disk I/O or other storage backends.
class IChunkPersistence {
public:
  virtual ~IChunkPersistence() = default;

  /// Request loading a chunk from storage. May be asynchronous.
  virtual void requestLoad(int32_t chunkX, int32_t chunkZ) = 0;

  /// Mark a chunk as dirty (needs saving).
  virtual void markDirty(int32_t chunkX, int32_t chunkZ) = 0;

  /// Record a manual terrain edit to the persistence layer.
  virtual void recordTerrainEdit(const TerrainBrush& brush) = 0;
};

} // namespace terrain
