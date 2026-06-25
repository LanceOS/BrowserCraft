#pragma once

#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "world/World.hpp"
#include "engine/save/SaveManager.hpp"
#include <memory>
#include <queue>
#include <mutex>
#include <string>

namespace voxel {

class BlockRegistry;

/// Manages world lifecycle: creation, save/load, and completion-job processing.
/// Owns the World instance and SaveManager, along with the completion queues
/// that worker threads use to signal finished gen/mesh jobs.
class WorldController {
public:
  WorldController(SharedPool& pool, BlockRegistry& blocks, const GameConfig& config);

  /// Create the World with the given callbacks (typically wired to thread pools by the caller).
  void createWorld(World::GenCallback onGenerate,
                   World::MeshCallback onMesh,
                   World::SaveLoadCallback onSaveLoad,
                   World::SaveDirtyCallback onMarkDirty);

  /// Configure save directory and create/replace the SaveManager.
  void configureSaveWorld(const std::string& saveDir, const std::string& slotId,
                          bool startFresh, WorkerThreadPool* ioPool);

  /// Drain completed gen/mesh jobs and notify the World.
  void processGenJobs();

  /// Flush and process pending save operations.
  void processSavePending();

  /// Called by worker thread callbacks when generation is done.
  void onGenCompleted(int32_t slotIndex) {
    std::lock_guard lock(m_completionMutex);
    m_completedGenSlots.push(slotIndex);
  }

  /// Called by worker thread callbacks when meshing is done.
  void onMeshCompleted(int32_t slotIndex) {
    std::lock_guard lock(m_completionMutex);
    m_completedMeshSlots.push(slotIndex);
  }

  [[nodiscard]] auto world() -> World& { return *m_world; }
  [[nodiscard]] auto world() const -> const World& { return *m_world; }
  [[nodiscard]] auto saveManager() -> SaveManager* { return m_saveManager.get(); }

  /// Clear the world (used when shutting down or switching worlds).
  void clearWorld() { if (m_world) m_world->clear(); }

private:
  SharedPool& m_pool;
  BlockRegistry& m_blocks;
  const GameConfig& m_config;

  std::unique_ptr<World> m_world;
  std::unique_ptr<SaveManager> m_saveManager;

  // Completion queues: workers push slot indices here.
  std::queue<int32_t> m_completedGenSlots;
  std::queue<int32_t> m_completedMeshSlots;
  std::mutex m_completionMutex;
};

} // namespace voxel
