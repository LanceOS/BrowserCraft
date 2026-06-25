#pragma once

#include "Chunk.hpp"
#include "IChunkWorker.hpp"
#include <deque>
#include <cstdint>
#include <unordered_map>

namespace voxel {

class ChunkManager;
class SharedPool;

/// Tracks mapping from slot index back to chunk coordinates.
/// Needed because map-backed chunk storage can rehash, making pointers unstable.
struct ChunkSlotCoord {
  int32_t cx;
  int32_t cz;
};

/// Manages the generation and meshing job queues for the world.
/// Owns the pending-gen and pending-mesh queues and their processing callbacks.
/// The pump() method processes all ready jobs, delegating to the IChunkWorker.
class ChunkJobQueue {
public:
  struct PendingChunkJob {
    int32_t slotIndex;
    int32_t chunkX;
    int32_t chunkZ;
  };

  explicit ChunkJobQueue(IChunkWorker& worker)
    : m_worker(worker) {}

  /// Push a new generation job.
  void pushGen(int32_t slotIndex, int32_t chunkX, int32_t chunkZ);

  /// Push a new mesh job.
  void pushMesh(int32_t slotIndex, int32_t chunkX, int32_t chunkZ);

  /// Process all pending jobs. Requires external dependencies for chunk/slot access.
  void pump(ChunkManager& chunks, SharedPool& pool,
            const std::unordered_map<int32_t, ChunkSlotCoord>& slotToChunk,
            uint32_t worldSeed);

  /// Clear all pending jobs.
  void clear();

  [[nodiscard]] auto genQueueSize() const -> size_t { return m_pendingGen.size(); }
  [[nodiscard]] auto meshQueueSize() const -> size_t { return m_pendingMesh.size(); }

private:
  /// Validate that a pending job's slot still points to the expected chunk coordinates.
  /// Returns nullptr if the slot has been reused for a different chunk.
  auto resolveChunk(const PendingChunkJob& job, ChunkManager& chunks,
                    const std::unordered_map<int32_t, ChunkSlotCoord>& slotToChunk) -> Chunk*;

  std::deque<PendingChunkJob> m_pendingGen;
  std::deque<PendingChunkJob> m_pendingMesh;
  IChunkWorker& m_worker;
};

} // namespace voxel
