#pragma once

#include "gl_core.hpp"
#include "PersistentBuffer.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

namespace voxel {

struct ChunkCullData {
    float min[4]; // vec4
    float max[4]; // vec4
    uint32_t indexCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t slotIndex;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
    uint32_t pad4;
};

struct DrawCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t baseInstance;
};

/// Builds indirect draw commands on the CPU from per-chunk cull data.
/// The compute-cull path was removed because CPU rebuilding is simpler and
/// more reliable across GPU drivers. With ~81 chunks at render distance 4,
/// the CPU cost is negligible and avoids an extra GPU dispatch + barrier.
class IndirectBatcher {
public:
  IndirectBatcher(uint32_t maxChunks) : m_maxChunks(maxChunks) {
      m_chunkDataBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(ChunkCullData), GL_SHADER_STORAGE_BUFFER);
      m_indirectBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(DrawCommand), GL_DRAW_INDIRECT_BUFFER);
      
      // Zero out
      std::memset(m_chunkDataBuffer->mappedPtr(), 0, m_chunkDataBuffer->capacity());
      std::memset(m_indirectBuffer->mappedPtr(), 0, m_indirectBuffer->capacity());
  }

  uint32_t capacity() const { return m_maxChunks; }

  void updateChunkData(uint32_t slotIndex, const ChunkCullData& data) {
      if (slotIndex >= m_maxChunks) return;
      auto* ptr = static_cast<ChunkCullData*>(m_chunkDataBuffer->mappedPtr());
      ptr[slotIndex] = data;
  }

  /// Rebuild indirect draw commands on the CPU from chunk metadata.
  /// No frustum culling is performed — all chunks with indexCount > 0 are drawn.
  /// The view-projection matrix parameter is retained for future culling integration.
  void buildIndirectCommands(const float* /*projViewMatrix*/) {
      auto* chunks = static_cast<ChunkCullData*>(m_chunkDataBuffer->mappedPtr());
      auto* commands = static_cast<DrawCommand*>(m_indirectBuffer->mappedPtr());
      std::memset(commands, 0, m_indirectBuffer->capacity());

      for (uint32_t i = 0; i < m_maxChunks; ++i) {
        const auto& c = chunks[i];
        if (c.indexCount == 0) continue;

        commands[i] = DrawCommand{
          c.indexCount,
          1u,
          c.firstIndex,
          c.baseVertex,
          c.slotIndex
        };
      }

      gl::MemoryBarrier(GL_COMMAND_BARRIER_BIT);
  }

  void drawIndirect() const {
      gl::BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_chunkDataBuffer->buffer());
      gl::BindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer->buffer());
      gl::MultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, m_maxChunks, sizeof(DrawCommand));
      gl::BindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  }

private:
  uint32_t m_maxChunks = 0;
  std::unique_ptr<PersistentBuffer> m_chunkDataBuffer;
  std::unique_ptr<PersistentBuffer> m_indirectBuffer;
};

} // namespace voxel
