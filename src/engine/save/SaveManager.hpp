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

namespace terrain {

class SharedPool;
class World;
class WorkerThreadPool;

/// Manages saving and loading chunk data (now delegated completely to seed-generation
/// and terrain edit replay). Implements IChunkPersistence for integration with World.
class SaveManager : public IChunkPersistence {
public:
  SaveManager(const std::string& saveDir, const std::string& slotId,
              SharedPool& pool, World& world, WorkerThreadPool* ioPool);
  ~SaveManager();

  SaveManager(const SaveManager&) = delete;
  SaveManager& operator=(const SaveManager&) = delete;

  /// Request a chunk to be loaded. Since block-grid chunk files are removed,
  /// this immediately reports failure so the world falls back to seed-generation
  /// and terrain edits replay.
  void requestLoad(int32_t chunkX, int32_t chunkZ) override;

  /// Mark a chunk as dirty. This is a no-op as individual chunk files are no longer saved.
  void markDirty(int32_t chunkX, int32_t chunkZ) override;

  /// Record a manual terrain edit to the persistence layer.
  void recordTerrainEdit(const TerrainBrush& brush) override;

  /// Flush any pending writes. Since chunk files are gone, this is a no-op.
  void flushPending() {}
  void processPending() {}

  // ---- Metadata management ----

  /// Get the world metadata for this save slot.
  [[nodiscard]] auto metadata() const -> const WorldMetadata& { return m_metadata; }

  /// Get the path to the metadata file for this save slot.
  [[nodiscard]] auto metadataFilePath() const -> std::string;

  /// Update and persist the world metadata.
  void writeMetadata(const WorldMetadata& meta);

  /// Touch the last-played timestamp to now and persist.
  void touchMetadata();

private:
  std::string m_saveDir;
  std::string m_slotId;
  SharedPool& m_pool;
  World& m_world;
  WorkerThreadPool* m_ioPool;
  mutable std::mutex m_mutex;

  WorldMetadata m_metadata;
};

} // namespace terrain
