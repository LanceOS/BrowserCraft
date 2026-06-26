#include "ChunkSyncer.hpp"
#include "IndirectBatcher.hpp"
#include "gl_core.hpp"
#include <algorithm>
#include <cstring>

namespace terrain {

ChunkSyncer::ChunkSyncer(ChunkMeshAllocator& meshAllocator,
                         IndirectBatcher& indirectBatcher,
                         const GameConfig& config)
  : m_meshAllocator(meshAllocator),
    m_indirectBatcher(indirectBatcher),
    m_config(config)
{}

void ChunkSyncer::sync(World& world) {
  const uint32_t capacity = m_indirectBatcher.capacity();
  bool uploadedAny = false;

  if (m_liveSlots.size() != static_cast<size_t>(m_meshAllocator.maxSlots())) {
    m_liveSlots.assign(static_cast<size_t>(m_meshAllocator.maxSlots()), 0u);
  } else {
    std::fill(m_liveSlots.begin(), m_liveSlots.end(), 0u);
  }

  world.forEachEntry([&](int64_t, const Chunk& chunk) {
    if (chunk.slotIndex >= 0 && chunk.slotIndex < static_cast<int32_t>(m_liveSlots.size())) {
      m_liveSlots[static_cast<size_t>(chunk.slotIndex)] = 1u;
    }
  });

  m_meshAllocator.releaseMissing(m_liveSlots, [&](int32_t slotIndex) {
    if (slotIndex < 0 || slotIndex >= static_cast<int32_t>(capacity)) return;
    ChunkCullData emptyData{};
    m_indirectBatcher.updateChunkData(static_cast<uint32_t>(slotIndex), emptyData);
  });

  // Upload newly-meshed chunks from the pending queue (avoids full chunk scan).
  std::vector<int32_t> pendingSlots = world.drainPendingUploadSlots();
  for (int32_t slotIndex : pendingSlots) {
    auto* chunk = world.getChunkBySlotIndex(slotIndex);
    if (!chunk) continue;
    if (chunk->state != ChunkState::MeshReady) continue;
    if (slotIndex < 0 || slotIndex >= static_cast<int32_t>(capacity)) {
      continue;
    }

    if (chunk->indexCount > 0 && chunk->vertexCount > 0) {
      auto alloc = m_meshAllocator.allocationForSlot(slotIndex);
      if (!alloc || !alloc->valid()) {
        continue;
      }
      auto slot = world.getChunkSlot(*chunk);

      ChunkCullData cullData{};
      const float borderPad = 1.0f;
      cullData.min[0] = static_cast<float>(chunk->chunkX * m_config.chunkSize) - borderPad;
      cullData.min[1] = 0.0f;
      cullData.min[2] = static_cast<float>(chunk->chunkZ * m_config.chunkSize) - borderPad;
      cullData.min[3] = 1.0f;
      cullData.max[0] = static_cast<float>(chunk->chunkX * m_config.chunkSize + m_config.chunkSize) + borderPad;
      cullData.max[1] = static_cast<float>(m_config.worldHeight);
      cullData.max[2] = static_cast<float>(chunk->chunkZ * m_config.chunkSize + m_config.chunkSize) + borderPad;
      cullData.max[3] = 1.0f;
      const uint32_t firstIndex = static_cast<uint32_t>(alloc->iboOffsetBytes / sizeof(uint32_t));
      cullData.opaqueIndexCount = chunk->opaqueIndexCount;
      cullData.opaqueFirstIndex = firstIndex;
      cullData.transparentIndexCount = chunk->transparentIndexCount;
      cullData.transparentFirstIndex = firstIndex + chunk->opaqueIndexCount;
      cullData.baseVertex = static_cast<uint32_t>(alloc->baseVertex);
      cullData.slotIndex = static_cast<uint32_t>(slotIndex);
      cullData.renderFlags = *slot.renderFlags;
      m_indirectBatcher.updateChunkData(static_cast<uint32_t>(slotIndex), cullData);
    } else {
      ChunkCullData emptyData{};
      m_indirectBatcher.updateChunkData(static_cast<uint32_t>(slotIndex), emptyData);
    }
    world.markUploaded(*chunk);
    uploadedAny = true;
  }

  if (uploadedAny) {
    gl::MemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
  }
}

} // namespace terrain
