#pragma once

#include "gl_core.hpp"
#include "PersistentBuffer.hpp"
#include "../math/Frustum.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstring>

namespace voxel {

struct ChunkCullData {
    float min[4]; // vec4
    float max[4]; // vec4
    uint32_t indexCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t slotIndex;
    uint32_t hasTransparent;
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
class IndirectBatcher {
public:
  IndirectBatcher(uint32_t maxChunks) : m_maxChunks(maxChunks) {
      m_chunkDataBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(ChunkCullData), GL_SHADER_STORAGE_BUFFER);
      m_indirectBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(DrawCommand), GL_DRAW_INDIRECT_BUFFER);

      // Zero out.
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
  /// Chunks are frustum-culled and partitioned into opaque/transparent groups.
  void buildIndirectCommands(const Frustum& frustum) {
      const auto* chunks = static_cast<const ChunkCullData*>(m_chunkDataBuffer->mappedPtr());
      auto* commands = static_cast<DrawCommand*>(m_indirectBuffer->mappedPtr());

      uint32_t visibleOpaque = 0u;
      uint32_t visibleTransparent = 0u;

      for (uint32_t i = 0; i < m_maxChunks; ++i) {
        const auto& c = chunks[i];
        if (c.indexCount == 0) continue;

        if (!frustum.intersectsAABB(c.min[0], c.min[1], c.min[2],
                                   c.max[0], c.max[1], c.max[2])) {
          continue;
        }

        const DrawCommand cmd{
          c.indexCount,
          1u,
          c.firstIndex,
          c.baseVertex,
          c.slotIndex
        };

        if (c.hasTransparent) {
          ++visibleTransparent;
        } else {
          ++visibleOpaque;
        }
      }

      const uint32_t opaqueCommandCount = visibleOpaque;
      const uint32_t transparentCommandCount = visibleTransparent;

      uint32_t opaqueWriteIndex = 0u;
      uint32_t transparentWriteIndex = opaqueCommandCount;
      for (uint32_t i = 0; i < m_maxChunks; ++i) {
        const auto& c = chunks[i];
        if (c.indexCount == 0) continue;
        if (!frustum.intersectsAABB(c.min[0], c.min[1], c.min[2],
                                   c.max[0], c.max[1], c.max[2])) {
          continue;
        }

        const DrawCommand cmd{
          c.indexCount,
          1u,
          c.firstIndex,
          c.baseVertex,
          c.slotIndex
        };

        if (c.hasTransparent) {
          commands[transparentWriteIndex++] = cmd;
        } else {
          commands[opaqueWriteIndex++] = cmd;
        }
      }

      m_commandCount = opaqueCommandCount + transparentCommandCount;
      m_opaqueCommandCount = opaqueCommandCount;
      m_transparentCommandCount = transparentCommandCount;
      gl::MemoryBarrier(GL_COMMAND_BARRIER_BIT);
  }

  auto opaqueCommandCount() const -> uint32_t { return m_opaqueCommandCount; }
  auto transparentCommandCount() const -> uint32_t { return m_transparentCommandCount; }
  auto commandCount() const -> uint32_t { return m_commandCount; }

  void drawIndirect(uint32_t commandCount, uint32_t firstCommand = 0u) const {
      if (commandCount == 0u) return;
      gl::BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_chunkDataBuffer->buffer());
      gl::BindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer->buffer());

      const void* indirectOffset = reinterpret_cast<const void*>(
        static_cast<std::size_t>(firstCommand) * sizeof(DrawCommand));
      gl::MultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, indirectOffset,
                                   static_cast<GLsizei>(commandCount), sizeof(DrawCommand));
      gl::BindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  }

private:
  uint32_t m_maxChunks = 0;
  uint32_t m_commandCount = 0u;
  uint32_t m_opaqueCommandCount = 0u;
  uint32_t m_transparentCommandCount = 0u;
  std::unique_ptr<PersistentBuffer> m_chunkDataBuffer;
  std::unique_ptr<PersistentBuffer> m_indirectBuffer;
};

} // namespace voxel
