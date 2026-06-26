#pragma once

#include "../../world/World.hpp"
#include "../../engine/core/Config.hpp"
#include "ChunkMeshAllocator.hpp"
#include <cstdint>
#include <vector>

namespace terrain {

class IndirectBatcher;

/// Handles CPU→GPU synchronization of chunk mesh data.
/// Iterates all chunks, reconciles compact mesh allocations, and updates the
/// indirect-batcher's per-chunk cull data.
class ChunkSyncer {
public:
  ChunkSyncer(ChunkMeshAllocator& meshAllocator,
              IndirectBatcher& indirectBatcher,
              const GameConfig& config);

  /// Sync all chunks that have new meshes and publish their draw metadata.
  void sync(World& world);

private:
  ChunkMeshAllocator& m_meshAllocator;
  IndirectBatcher& m_indirectBatcher;
  GameConfig m_config;
  std::vector<uint8_t> m_liveSlots;
};

} // namespace terrain
