#include "WorldController.hpp"
#include <filesystem>
#include <algorithm>

namespace terrain {

WorldController::WorldController(SharedPool& pool, const GameConfig& config)
  : m_pool(pool), m_config(config)
{}

void WorldController::createWorld(IChunkWorker& worker, IChunkPersistence* persistence) {
  m_world = std::make_unique<World>(m_pool, m_config, worker, persistence);
}

void WorldController::configureSaveWorld(const std::string& saveDir, const std::string& slotId,
                                          bool startFresh, WorkerThreadPool* ioPool) {
  std::string selectedSlot = slotId.empty() ? "default" : slotId;

  {
    std::lock_guard lock(m_completionMutex);
    std::queue<int32_t> emptyGen;
    std::queue<CompletedMeshJob> emptyMesh;
    m_completedGenSlots.swap(emptyGen);
    m_completedMeshSlots.swap(emptyMesh);
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
  m_world->attachPersistence(m_saveManager.get());
}

void WorldController::processGenJobs() {
  std::queue<int32_t> genSlots;
  std::queue<CompletedMeshJob> meshSlots;
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
    CompletedMeshJob job = meshSlots.front();
    meshSlots.pop();
    int32_t i = job.slotIndex;
    auto* chunk = m_world->getChunkBySlotIndex(i);
    if (chunk && chunk->state == ChunkState::Meshing) {
      auto slot = m_pool.view(i);
      uint32_t renderFlags = *slot.renderFlags;
      chunk->opaqueIndexCount = *slot.opaqueIndexCount;
      chunk->transparentIndexCount = *slot.transparentIndexCount;
      chunk->hasOpaque = (renderFlags & CHUNK_RENDER_FLAG_HAS_OPAQUE) != 0u;
      chunk->hasTransparent = (renderFlags & CHUNK_RENDER_FLAG_HAS_TRANSPARENT) != 0u;
      chunk->terrainCollision = std::move(job.terrainCollision);
      m_world->onMeshDone(i, *slot.vertexCount, *slot.indexCount, job.success);
    }
  }
}

void WorldController::processSavePending() {
  if (m_saveManager) {
    m_saveManager->processPending();
  }
}

} // namespace terrain
