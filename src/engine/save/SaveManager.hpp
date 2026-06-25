#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <functional>

namespace voxel {

class SharedPool;
class World;

/// Manages saving and loading chunk data to/from disk.
class SaveManager {
public:
  using LoadCallback = std::function<void(int32_t chunkX, int32_t chunkZ)>;
  using LoadSuccessCallback = std::function<void(int32_t, int32_t, const uint8_t*, const uint8_t*, const uint8_t*, size_t)>;
  using LoadFailCallback = std::function<void(int32_t, int32_t)>;

  SaveManager(const std::string& saveDir, const std::string& slotId,
              SharedPool& pool, World& world);
  ~SaveManager();

  SaveManager(const SaveManager&) = delete;
  SaveManager& operator=(const SaveManager&) = delete;

  /// Request a chunk to be loaded from disk (async via thread pool or sync).
  void requestLoad(int32_t chunkX, int32_t chunkZ);

  /// Mark a chunk as needing to be saved.
  void markDirty(int32_t chunkX, int32_t chunkZ);

  /// Flush all pending saves to disk (call on quit).
  void flushPending();

  /// Save a single chunk immediately.
  auto saveChunk(int32_t chunkX, int32_t chunkZ) -> bool;

  /// Load a single chunk immediately. Returns true if found.
  auto loadChunk(int32_t chunkX, int32_t chunkZ,
                 uint8_t* outVoxels, uint8_t* outLight, uint8_t* outRedstone,
                 size_t dataSize) -> bool;

  /// Process pending loads (call each frame on main thread).
  void processPending();

private:
  auto chunkFilePath(int32_t cx, int32_t cz) const -> std::string;

  std::string m_saveDir;
  std::string m_slotId;
  SharedPool& m_pool;
  World& m_world;
  std::unordered_set<int64_t> m_dirtyChunks;
  std::mutex m_mutex;
  size_t m_dataSize = 0;
};

} // namespace voxel
