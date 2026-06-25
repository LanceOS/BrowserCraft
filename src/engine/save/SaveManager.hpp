#pragma once

#include "world/IChunkPersistence.hpp"
#include "WorldMetadata.hpp"
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <functional>
#include <memory>
#include <optional>

namespace voxel {

class SharedPool;
class World;
class WorkerThreadPool;

/// Holds the result of an async chunk load from disk.
struct PendingChunkLoad {
  int32_t chunkX;
  int32_t chunkZ;
  std::vector<uint8_t> voxels;
  std::vector<uint8_t> light;
  std::vector<uint8_t> redstone;
  bool success;
};

/// Manages saving and loading chunk data to/from disk.
/// Implements IChunkPersistence for integration with World.
class SaveManager : public IChunkPersistence {
public:
  using LoadCallback = std::function<void(int32_t chunkX, int32_t chunkZ)>;
  using LoadSuccessCallback = std::function<void(int32_t, int32_t, const uint8_t*, const uint8_t*, const uint8_t*, size_t)>;
  using LoadFailCallback = std::function<void(int32_t, int32_t)>;

  SaveManager(const std::string& saveDir, const std::string& slotId,
              SharedPool& pool, World& world, WorkerThreadPool* ioPool);
  ~SaveManager();

  SaveManager(const SaveManager&) = delete;
  SaveManager& operator=(const SaveManager&) = delete;

  /// Request a chunk to be loaded from disk (async via thread pool).
  void requestLoad(int32_t chunkX, int32_t chunkZ) override;

  /// Mark a chunk as needing to be saved. Saves are submitted asynchronously
  /// to the I/O thread pool so the main thread is not blocked.
  void markDirty(int32_t chunkX, int32_t chunkZ) override;

  /// Flush all pending saves to disk synchronously (call on quit).
  void flushPending();

  /// Save a single chunk immediately.
  auto saveChunk(int32_t chunkX, int32_t chunkZ) -> bool;

  /// Load a single chunk immediately. Returns true if found.
  auto loadChunk(int32_t chunkX, int32_t chunkZ,
                 uint8_t* outVoxels, uint8_t* outLight, uint8_t* outRedstone,
                 size_t dataSize) -> bool;

  /// Process pending loads (call each frame on main thread).
  void processPending();

  // ---- Metadata management ----

  /// Get the world metadata for this save slot.
  [[nodiscard]] auto metadata() const -> const WorldMetadata& { return m_metadata; }

  /// Get the path to the metadata file for this save slot.
  [[nodiscard]] auto metadataFilePath() const -> std::string;

  /// Update and persist the world metadata (e.g., timestamp on load).
  void writeMetadata(const WorldMetadata& meta);

  /// Touch the last-played timestamp to now and persist.
  void touchMetadata();

private:
  auto chunkFilePath(int32_t cx, int32_t cz) const -> std::string;

  std::string m_saveDir;
  std::string m_slotId;
  SharedPool& m_pool;
  World& m_world;
  WorkerThreadPool* m_ioPool;
  std::unordered_set<int64_t> m_dirtyChunks;
  std::queue<PendingChunkLoad> m_pendingLoads;
  mutable std::mutex m_mutex;
  mutable std::mutex m_loadMutex;
  size_t m_dataSize = 0;

  WorldMetadata m_metadata;
};

} // namespace voxel
