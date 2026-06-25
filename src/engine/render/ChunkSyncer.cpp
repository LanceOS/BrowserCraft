#include "ChunkSyncer.hpp"
#include "PersistentBuffer.hpp"
#include "IndirectBatcher.hpp"
#include "gl_core.hpp"
#include <cstring>

namespace voxel {

ChunkSyncer::ChunkSyncer(PersistentBuffer* masterVbo, PersistentBuffer* masterIbo,
                          IndirectBatcher* indirectBatcher, const GameConfig& config)
  : m_masterVbo(masterVbo), m_masterIbo(masterIbo),
    m_indirectBatcher(indirectBatcher), m_config(config)
{}

void ChunkSyncer::sync(World& world) {
  const uint32_t capacity = m_indirectBatcher->capacity();
  bool uploadedAny = false;

  // Upload newly-meshed chunks from the pending queue (avoids full chunk scan).
  std::vector<int32_t> pendingSlots = world.drainPendingUploadSlots();
  for (int32_t slotIndex : pendingSlots) {
    auto* chunk = world.getChunkBySlotIndex(slotIndex);
    if (!chunk) continue;
    if (chunk->state != ChunkState::MeshReady) continue;
    auto slot = world.getChunkSlot(*chunk);
    if (slot.slotIndex < 0 || slot.slotIndex >= static_cast<int32_t>(capacity)) {
      continue;
    }

    if (chunk->indexCount > 0 && chunk->vertexCount > 0) {
      const int32_t stride = m_config.vertexStrideFloats;

      size_t vboOffset = static_cast<size_t>(slot.slotIndex) * m_config.maxVertsPerChunk * stride * sizeof(float);
      size_t iboOffset = static_cast<size_t>(slot.slotIndex) * m_config.maxIndicesPerChunk * sizeof(uint32_t);
      int32_t baseVertex = slot.slotIndex * m_config.maxVertsPerChunk;

      // Directly copy vertices and indices to the persistently mapped GPU memory
      std::memcpy(static_cast<uint8_t*>(m_masterVbo->mappedPtr()) + vboOffset,
                  slot.vertices, static_cast<size_t>(chunk->vertexCount) * stride * sizeof(float));
      std::memcpy(static_cast<uint8_t*>(m_masterIbo->mappedPtr()) + iboOffset,
                  slot.indices, chunk->indexCount * sizeof(uint32_t));

      // Ensure GPU sees the uploaded vertex/index data before drawing
      gl::MemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);

      ChunkCullData cullData{};
      cullData.min[0] = static_cast<float>(chunk->chunkX * m_config.chunkSize);
      cullData.min[1] = 0.0f;
      cullData.min[2] = static_cast<float>(chunk->chunkZ * m_config.chunkSize);
      cullData.min[3] = 1.0f;
      cullData.max[0] = cullData.min[0] + static_cast<float>(m_config.chunkSize);
      cullData.max[1] = static_cast<float>(m_config.worldHeight);
      cullData.max[2] = cullData.min[2] + static_cast<float>(m_config.chunkSize);
      cullData.max[3] = 1.0f;
      cullData.indexCount = chunk->indexCount;
      cullData.firstIndex = static_cast<uint32_t>(iboOffset / sizeof(uint32_t));
      cullData.baseVertex = static_cast<uint32_t>(baseVertex);
      cullData.slotIndex = static_cast<uint32_t>(slot.slotIndex);
      cullData.hasTransparent = static_cast<uint32_t>(chunk->hasTransparent);
      m_indirectBatcher->updateChunkData(slot.slotIndex, cullData);
    } else {
      ChunkCullData emptyData{};
      emptyData.hasTransparent = 0u;
      m_indirectBatcher->updateChunkData(slot.slotIndex, emptyData);
    }
    world.markUploaded(*chunk);
    uploadedAny = true;
  }

  if (uploadedAny) {
    gl::MemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
  }

  return;
}

} // namespace voxel
