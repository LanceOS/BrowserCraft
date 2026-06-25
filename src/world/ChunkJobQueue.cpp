#include "ChunkJobQueue.hpp"
#include "ChunkManager.hpp"
#include "engine/alloc/SharedPool.hpp"
#include <cstdint>

namespace voxel {

ChunkJobQueue::ChunkJobQueue(GenCallback onGenerate, MeshCallback onMesh)
  : m_onGenerate(std::move(onGenerate)),
    m_onMesh(std::move(onMesh))
{}

void ChunkJobQueue::pushGen(int32_t slotIndex, int32_t chunkX, int32_t chunkZ) {
  m_pendingGen.push_back(PendingChunkJob{slotIndex, chunkX, chunkZ});
}

void ChunkJobQueue::pushMesh(int32_t slotIndex, int32_t chunkX, int32_t chunkZ) {
  m_pendingMesh.push_back(PendingChunkJob{slotIndex, chunkX, chunkZ});
}

auto ChunkJobQueue::resolveChunk(
    const PendingChunkJob& job,
    ChunkManager& chunks,
    const std::unordered_map<int32_t, ChunkSlotCoord>& slotToChunk) -> Chunk* {
  auto it = slotToChunk.find(job.slotIndex);
  if (it == slotToChunk.end()) return nullptr;
  auto* chunk = chunks.getMut(it->second.cx, it->second.cz);
  if (!chunk) return nullptr;
  if (chunk->chunkX != job.chunkX || chunk->chunkZ != job.chunkZ) return nullptr;
  return chunk;
}

void ChunkJobQueue::pump(
    ChunkManager& chunks,
    SharedPool& pool,
    const std::unordered_map<int32_t, ChunkSlotCoord>& slotToChunk,
    uint32_t worldSeed) {
  // World-gen queue
  while (!m_pendingGen.empty()) {
    PendingChunkJob job = m_pendingGen.front();
    m_pendingGen.pop_front();

    auto* chunk = resolveChunk(job, chunks, slotToChunk);
    if (!chunk) {
      continue;
    }

    auto slot = pool.view(chunk->slotIndex);
    *slot.status = static_cast<int32_t>(ChunkSlotStatus::GENERATING);
    chunk->state = ChunkState::Generating;
    m_onGenerate(chunk->slotIndex, chunk->chunkX, chunk->chunkZ,
                 chunkSeed(chunk->chunkX, chunk->chunkZ, worldSeed));
  }

  // Mesh queue
  while (!m_pendingMesh.empty()) {
    PendingChunkJob job = m_pendingMesh.front();
    m_pendingMesh.pop_front();

    auto* chunk = resolveChunk(job, chunks, slotToChunk);
    if (!chunk) {
      continue;
    }

    auto slot = pool.view(chunk->slotIndex);
    *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESHING);
    chunk->state = ChunkState::Meshing;
    m_onMesh(chunk->slotIndex);
  }
}

void ChunkJobQueue::clear() {
  m_pendingGen.clear();
  m_pendingMesh.clear();
}

} // namespace voxel
