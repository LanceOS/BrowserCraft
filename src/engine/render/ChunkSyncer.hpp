#pragma once

#include "../../world/World.hpp"
#include "../../engine/core/Config.hpp"
#include <cstdint>
#include <vector>

namespace voxel {

class PersistentBuffer;
class IndirectBatcher;

/// Handles CPU→GPU synchronization of chunk mesh data.
/// Iterates all chunks, copies vertex/index data into persistently-mapped
/// GPU buffers, and updates the indirect-batcher's per-chunk cull data.
class ChunkSyncer {
public:
  ChunkSyncer(PersistentBuffer* masterVbo, PersistentBuffer* masterIbo,
              IndirectBatcher* indirectBatcher, const GameConfig& config);

  /// Sync all chunks that have new meshes. Returns true if any transparent
  /// chunks are present (caller should use this to decide multi-pass rendering).
  bool sync(World& world);

private:
  PersistentBuffer* m_masterVbo;
  PersistentBuffer* m_masterIbo;
  IndirectBatcher* m_indirectBatcher;
  GameConfig m_config;
};

} // namespace voxel
