#pragma once

#include "Chunk.hpp"
#include "ChunkManager.hpp"
#include "ChunkJobQueue.hpp"
#include "IChunkWorker.hpp"
#include "IChunkPersistence.hpp"
#include "BlockRegistry.hpp"
#include "RemeshScheduler.hpp"
#include "VoxelStore.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <optional>
#include <functional>
#include <utility>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

struct WorldBlockRef {
  const Chunk* chunk;
  int32_t localX;
  int32_t localZ;
  int32_t index; // flat index into voxel array
};

/// Manages chunk lifecycle: generation, meshing, loading, unloading.
/// Worker operations use IChunkWorker and IChunkPersistence interfaces.
class World {
public:
  /// Access the job queue for fine-grained control.
  [[nodiscard]] auto jobQueue() -> ChunkJobQueue& { return m_jobQueue; }

  World(SharedPool& pool,
        BlockRegistry& blocks,
        const GameConfig& config,
        IChunkWorker& worker,
        IChunkPersistence* persistence);

  /// Called each frame with the camera position.
  void update(glm::vec3 cameraPosition);

  /// Get block ID at world coordinates.
  [[nodiscard]] auto getBlockIdAt(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t;

  /// Set block ID at world coordinates. Returns true if changed.
  auto setBlockIdAt(int32_t worldX, int32_t worldY, int32_t worldZ, uint8_t blockId) -> bool;

  /// Get redstone value at world coords.
  [[nodiscard]] auto getRedstonePackedAt(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t;

  /// Set redstone value at world coords. Returns true if changed.
  auto setRedstonePackedAt(int32_t worldX, int32_t worldY, int32_t worldZ, uint8_t packed) -> bool;

  /// Check if a world position has a solid block.
  [[nodiscard]] auto isSolid(int32_t worldX, int32_t worldY, int32_t worldZ) const -> bool;

  /// Fast-path: check solidity using a pre-resolved chunk (skips chunk lookup).
  /// The caller must ensure worldX/worldZ fall within the given chunk.
  [[nodiscard]] auto isSolidInChunk(int32_t worldX, int32_t worldY, int32_t worldZ, const Chunk& chunk) const -> bool;

  /// Check if a world position has a liquid block.
  [[nodiscard]] auto isFluid(int32_t worldX, int32_t worldY, int32_t worldZ) const -> bool;

  /// Check if the world is fully render-ready (center chunk mesh has been uploaded).
  [[nodiscard]] auto isReady() const -> bool;

  /// Check if the world has terrain data for the center chunk for gameplay/spawn logic.
  [[nodiscard]] auto hasTerrain() const -> bool;

  /// Attach a persistence backend for loading/saving chunks.
  void attachPersistence(IChunkPersistence* persistence) { m_persistence = persistence; }

  /// Get chunk by coordinates.
  [[nodiscard]] auto getChunk(int32_t cx, int32_t cz) const -> const Chunk*;
  [[nodiscard]] auto getChunkMut(int32_t cx, int32_t cz) -> Chunk*;

  /// Get chunk by slot index.
  [[nodiscard]] auto getChunkBySlotIndex(int32_t slotIndex) const -> const Chunk*;
  [[nodiscard]] auto getChunkBySlotIndex(int32_t slotIndex) -> Chunk*;

  /// Get chunk slot view.
  [[nodiscard]] auto getChunkSlot(const Chunk& chunk) -> ChunkSlot;

  /// Resolve world coords to a block reference.
  [[nodiscard]] auto resolveBlock(int32_t worldX, int32_t worldY, int32_t worldZ) const -> std::optional<WorldBlockRef>;

  /// Mark a chunk as uploaded to GPU.
  void markUploaded(const Chunk& chunk);
  /// Remove all currently loaded chunks and reset world state.
  void clear();

  /// Signal that a world-gen job completed.
  void onWorldGenDone(int32_t slotIndex);

  /// Signal that a mesh job completed.
  void onMeshDone(int32_t slotIndex, uint32_t vertexCount, uint32_t indexCount, bool success);

  /// Drain the queue of slots whose meshes are ready for GPU upload.
  /// Called each frame by ChunkSyncer to avoid iterating all chunks.
  auto drainPendingUploadSlots() -> std::vector<int32_t>;

  /// Signal save-load success with voxel/light/redstone data.
  void onSaveLoadSuccess(int32_t chunkX, int32_t chunkZ,
                         const uint8_t* voxels, const uint8_t* light, const uint8_t* redstone,
                         size_t dataSize);

  /// Signal save-load failed — fall back to generation.
  void onSaveLoadFailed(int32_t chunkX, int32_t chunkZ);

  /// Iterate over all chunks.
  template <typename F>
  void forEachChunk(F&& cb) { m_chunks.forEach(std::forward<F>(cb)); }

  /// Iterate over (key, chunk) entries.
  template <typename F>
  void forEachEntry(F&& cb) const { m_chunks.forEachEntry(std::forward<F>(cb)); }

  template <typename F>
  void forEachEntry(F&& cb) { m_chunks.forEachEntry(std::forward<F>(cb)); }

  /// Access the low-level voxel store.
  [[nodiscard]] auto store() -> VoxelStore& { return m_store; }
  [[nodiscard]] auto store() const -> const VoxelStore& { return m_store; }

  [[nodiscard]] auto hasChunkKey(int64_t key) const -> bool { return m_chunks.hasKey(key); }
  [[nodiscard]] auto chunkCount() const -> size_t { return m_chunks.size(); }

  [[nodiscard]] auto config() const -> const GameConfig& { return m_config; }
  void requestRemesh(Chunk& chunk);
  void markChunkDirty(int32_t cx, int32_t cz);

private:
  void ensureVisibleRadius(int32_t centerCX, int32_t centerCZ);
  void unloadFarChunks(int32_t centerCX, int32_t centerCZ);
  void restartChunkFromScratch(Chunk& chunk);
  void requestNeighborRemeshes(const Chunk& chunk);
  void requestBoundaryNeighborRemeshes(const Chunk& chunk, int32_t localX, int32_t localZ);
  void requestNeighborRemesh(const Chunk& chunk, int32_t dx, int32_t dz);

  [[nodiscard]] auto chunkHasVoxelData(const Chunk& chunk) const -> bool;
  [[nodiscard]] auto chunkHasMeshes(const Chunk& chunk) const -> bool;
  [[nodiscard]] auto getBlockId(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t;

  SharedPool& m_pool;
  BlockRegistry& m_blocks;
  const GameConfig& m_config;

  ChunkManager m_chunks;
  VoxelStore m_store;
  // @see notes/chunk-slot-mapping-stability.md
  std::unordered_map<int32_t, ChunkSlotCoord> m_slotToChunk;

  ChunkJobQueue m_jobQueue;

  IChunkPersistence* m_persistence = nullptr;
  std::vector<int32_t> m_pendingUploadSlots;
  // Boundary edits collected during a frame — flushed at end of update()
  RemeshScheduler m_remeshScheduler;

  int32_t m_centerChunkX = 0;
  int32_t m_centerChunkZ = 0;
  bool m_hasCenter = false;
};

} // namespace voxel
