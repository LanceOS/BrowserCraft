#pragma once

#include "Chunk.hpp"
#include "ChunkManager.hpp"
#include "BlockRegistry.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <optional>
#include <functional>

namespace voxel {

/// Utility: world coordinate → chunk coordinate.
inline auto worldToChunk(float coord, int32_t size) -> int32_t {
  return static_cast<int32_t>(std::floor(coord / static_cast<float>(size)));
}

/// Utility: positive modulo.
inline auto mod(int32_t value, int32_t size) -> int32_t {
  int32_t r = value % size;
  return r < 0 ? r + size : r;
}

/// Deterministic chunk seed for world generation.
inline auto chunkSeed(int32_t chunkX, int32_t chunkZ, uint32_t seed) -> uint32_t {
  uint32_t h = seed ^ static_cast<uint32_t>(chunkX) * 374761393u
                      ^ static_cast<uint32_t>(chunkZ) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h;
}

struct WorldBlockRef {
  const Chunk* chunk;
  int32_t localX;
  int32_t localZ;
  int32_t index; // flat index into voxel array
};

/// Manages chunk lifecycle: generation, meshing, loading, unloading.
/// This is the core world class. Worker/save integration uses function callbacks.
class World {
public:
  /// Callback types for worker and save operations.
  using GenCallback = std::function<void(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed)>;
  using MeshCallback = std::function<void(int32_t slotIndex)>;
  using SaveLoadCallback = std::function<void(int32_t chunkX, int32_t chunkZ)>;
  using SaveDirtyCallback = std::function<void(int32_t chunkX, int32_t chunkZ)>;

  World(SharedPool& pool,
        BlockRegistry& blocks,
        const GameConfig& config,
        GenCallback onGenerate,
        MeshCallback onMesh,
        SaveLoadCallback onSaveLoad,
        SaveDirtyCallback onMarkDirty);

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

  /// Check if a world position has a liquid block.
  [[nodiscard]] auto isFluid(int32_t worldX, int32_t worldY, int32_t worldZ) const -> bool;

  /// Check if the world is ready (center chunk has voxel data available for gameplay logic).
  [[nodiscard]] auto isReady() const -> bool;

  /// Attach a save manager for loading/saving chunks.
  void attachSaveManager(void* saveManager) { m_saveManager = saveManager; }

  /// Get chunk by coordinates.
  [[nodiscard]] auto getChunk(int32_t cx, int32_t cz) const -> const Chunk*;

  /// Get chunk by slot index.
  [[nodiscard]] auto getChunkBySlotIndex(int32_t slotIndex) const -> const Chunk*;

  /// Get chunk slot view.
  [[nodiscard]] auto getChunkSlot(const Chunk& chunk) -> ChunkSlot;

  /// Resolve world coords to a block reference.
  [[nodiscard]] auto resolveBlock(int32_t worldX, int32_t worldY, int32_t worldZ) -> std::optional<WorldBlockRef>;

  /// Mark a chunk as uploaded to GPU.
  void markUploaded(const Chunk& chunk);
  /// Remove all currently loaded chunks and reset world state.
  void clear();

  /// Signal that a world-gen job completed.
  void onWorldGenDone(int32_t slotIndex);

  /// Signal that a mesh job completed.
  void onMeshDone(int32_t slotIndex, uint32_t vertexCount, uint32_t indexCount, bool success);

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

  [[nodiscard]] auto hasChunkKey(const std::string& key) const -> bool { return m_chunks.hasKey(key); }
  [[nodiscard]] auto chunkCount() const -> size_t { return m_chunks.size(); }

private:
  void ensureVisibleRadius(int32_t centerCX, int32_t centerCZ);
  void unloadFarChunks(int32_t centerCX, int32_t centerCZ);
  void pumpQueues();
  void requestRemesh(Chunk& chunk);
  void markChunkDirty(int32_t cx, int32_t cz);
  void requestNeighborRemeshes(const Chunk& chunk);
  void requestBoundaryNeighborRemeshes(const Chunk& chunk, int32_t localX, int32_t localZ);
  void requestNeighborRemesh(const Chunk& chunk, int32_t dx, int32_t dz);

  [[nodiscard]] auto chunkHasVoxelData(const Chunk& chunk) const -> bool;
  [[nodiscard]] auto getBlockId(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t;

  SharedPool& m_pool;
  BlockRegistry& m_blocks;
  const GameConfig& m_config;

  ChunkManager m_chunks;
  std::unordered_map<int32_t, Chunk*> m_slotToChunk;
  std::vector<Chunk*> m_pendingGen;
  std::vector<Chunk*> m_pendingMesh;

  int32_t m_centerChunkX = 0;
  int32_t m_centerChunkZ = 0;
  bool m_hasCenter = false;

  GenCallback m_onGenerate;
  MeshCallback m_onMesh;
  SaveLoadCallback m_onSaveLoad;
  SaveDirtyCallback m_onMarkDirty;
  void* m_saveManager = nullptr;
};

} // namespace voxel
