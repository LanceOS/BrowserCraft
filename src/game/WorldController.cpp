#include "WorldController.hpp"
#include <filesystem>
#include <algorithm>

namespace voxel {

WorldController::WorldController(SharedPool& pool, BlockRegistry& blocks, const GameConfig& config)
  : m_pool(pool), m_blocks(blocks), m_config(config)
{}

void WorldController::createWorld(World::GenCallback onGenerate,
                                   World::MeshCallback onMesh,
                                   World::SaveLoadCallback onSaveLoad,
                                   World::SaveDirtyCallback onMarkDirty) {
  m_world = std::make_unique<World>(m_pool, m_blocks, m_config,
                                     std::move(onGenerate),
                                     std::move(onMesh),
                                     std::move(onSaveLoad),
                                     std::move(onMarkDirty));
}

void WorldController::configureSaveWorld(const std::string& saveDir, const std::string& slotId,
                                          bool startFresh, WorkerThreadPool* ioPool) {
  std::string selectedSlot = slotId.empty() ? "default" : slotId;

  if (m_saveManager && !startFresh) {
    m_saveManager->flushPending();
  }
  m_saveManager.reset();

  if (m_world) {
    m_world->clear();
  }

  if (startFresh) {
    std::error_code ec;
    std::filesystem::remove_all(saveDir + "/" + selectedSlot, ec);
  }

  m_saveManager = std::make_unique<SaveManager>(saveDir, selectedSlot, m_pool, *m_world, ioPool);
  m_world->attachSaveManager(m_saveManager.get());
}

void WorldController::processGenJobs() {
  std::queue<int32_t> genSlots;
  std::queue<int32_t> meshSlots;
  {
    std::lock_guard lock(m_completionMutex);
    genSlots.swap(m_completedGenSlots);
    meshSlots.swap(m_completedMeshSlots);
  }

  while (!genSlots.empty()) {
    int32_t i = genSlots.front();
    genSlots.pop();
    auto* chunk = m_world->getChunkBySlotIndex(i);
    if (chunk && chunk->state == ChunkState::Generating) {
      m_world->onWorldGenDone(i);
    }
  }

  while (!meshSlots.empty()) {
    int32_t i = meshSlots.front();
    meshSlots.pop();
    auto* chunk = m_world->getChunkBySlotIndex(i);
    if (chunk && chunk->state == ChunkState::Meshing) {
      auto slot = m_pool.view(i);
      int32_t rawStatus = *slot.status;
      chunk->hasTransparent = (rawStatus & 0x10000) != 0;
      m_world->onMeshDone(i, *slot.vertexCount, *slot.indexCount, true);
    }
  }
}

void WorldController::processSavePending() {
  if (m_saveManager) {
    m_saveManager->processPending();
  }
}

} // namespace voxel
