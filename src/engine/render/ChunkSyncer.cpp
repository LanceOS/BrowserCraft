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

bool ChunkSyncer::sync(World& world) {
  const uint32_t capacity = m_indirectBatcher->capacity();
  std::vector<bool> activeSlots(capacity, false);
  bool hasTransparentChunks = false;

  // Upload new/updated meshes
  world.forEachEntry([&](int64_t key, Chunk& chunk) {
    if (chunk.hasTransparent) {
      hasTransparentChunks = true;
    }
    auto slot = world.getChunkSlot(chunk);
    // @see notes/renderer-slot-index-bounds.md
    if (slot.slotIndex < 0 || slot.slotIndex >= static_cast<int32_t>(capacity)) {
      return;
    }

    activeSlots[slot.slotIndex] = true;

    if (chunk.state == ChunkState::MeshReady) {
      if (chunk.indexCount > 0 && chunk.vertexCount > 0) {
        const int32_t stride = m_config.vertexStrideFloats;

        size_t vboOffset = static_cast<size_t>(slot.slotIndex) * m_config.maxVertsPerChunk * stride * sizeof(float);
        size_t iboOffset = static_cast<size_t>(slot.slotIndex) * m_config.maxIndicesPerChunk * sizeof(uint32_t);
        int32_t baseVertex = slot.slotIndex * m_config.maxVertsPerChunk;

        // Directly copy vertices and indices to the persistently mapped GPU memory
        std::memcpy(static_cast<uint8_t*>(m_masterVbo->mappedPtr()) + vboOffset,
                    slot.vertices, static_cast<size_t>(chunk.vertexCount) * stride * sizeof(float));
        std::memcpy(static_cast<uint8_t*>(m_masterIbo->mappedPtr()) + iboOffset,
                    slot.indices, chunk.indexCount * sizeof(uint32_t));

        ChunkCullData cullData{};
        cullData.min[0] = static_cast<float>(chunk.chunkX * m_config.chunkSize);
        cullData.min[1] = 0.0f;
        cullData.min[2] = static_cast<float>(chunk.chunkZ * m_config.chunkSize);
        cullData.min[3] = 1.0f;
        cullData.max[0] = cullData.min[0] + static_cast<float>(m_config.chunkSize);
        cullData.max[1] = static_cast<float>(m_config.worldHeight);
        cullData.max[2] = cullData.min[2] + static_cast<float>(m_config.chunkSize);
        cullData.max[3] = 1.0f;
        cullData.indexCount = chunk.indexCount;
        cullData.firstIndex = static_cast<uint32_t>(iboOffset / sizeof(uint32_t));
        cullData.baseVertex = static_cast<uint32_t>(baseVertex);
        cullData.slotIndex = static_cast<uint32_t>(slot.slotIndex);
        m_indirectBatcher->updateChunkData(slot.slotIndex, cullData);
      } else {
        ChunkCullData emptyData{};
        m_indirectBatcher->updateChunkData(slot.slotIndex, emptyData);
      }
      world.markUploaded(chunk);
    }
  });

  // Zero out slots that are no longer active
  for (uint32_t i = 0; i < capacity; ++i) {
    if (!activeSlots[i]) {
       ChunkCullData emptyData{};
       m_indirectBatcher->updateChunkData(i, emptyData);
    }
  }

  return hasTransparentChunks;
}

} // namespace voxel
