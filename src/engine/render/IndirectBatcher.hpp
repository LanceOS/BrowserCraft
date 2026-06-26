#pragma once

#include "gl_core.hpp"
#include "PersistentBuffer.hpp"
#include "../math/Frustum.hpp"
#include "../../world/ChunkTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstring>
#include <atomic>

namespace terrain {

struct ChunkCullData {
    float min[4]; // vec4
    float max[4]; // vec4
    uint32_t opaqueIndexCount;
    uint32_t opaqueFirstIndex;
    uint32_t transparentIndexCount;
    uint32_t transparentFirstIndex;
    uint32_t baseVertex;
    uint32_t slotIndex;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t renderFlags;
    uint32_t pad3;
    uint32_t pad4;
    uint32_t pad5;
};

struct DrawCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t baseInstance;
};

/// Builds indirect draw commands on the CPU from per-chunk cull data.
/// Emits opaque and transparent command streams into disjoint regions of the
/// indirect buffer so each draw pass only issues the geometry it needs.
class IndirectBatcher {
public:
  IndirectBatcher(uint32_t maxChunks) : m_maxChunks(maxChunks) {
      m_chunkDataBuffer = std::make_unique<PersistentBuffer>(maxChunks * sizeof(ChunkCullData), GL_SHADER_STORAGE_BUFFER);
      // Two command streams are stored back-to-back:
      // terrain opaque, terrain transparent.
      m_indirectBuffer = std::make_unique<PersistentBuffer>(maxChunks * 2u * sizeof(DrawCommand), GL_DRAW_INDIRECT_BUFFER);

      // Zero out.
      std::memset(m_chunkDataBuffer->mappedPtr(), 0, m_chunkDataBuffer->capacity());
      std::memset(m_indirectBuffer->mappedPtr(), 0, m_indirectBuffer->capacity());
  }

  uint32_t capacity() const { return m_maxChunks; }
  uint32_t terrainOpaqueCommandBase() const { return 0u; }
  uint32_t terrainTransparentCommandBase() const { return m_maxChunks; }

  void updateChunkData(uint32_t slotIndex, const ChunkCullData& data) {
      if (slotIndex >= m_maxChunks) return;
      auto* ptr = static_cast<ChunkCullData*>(m_chunkDataBuffer->mappedPtr());
      ptr[slotIndex] = data;
      // Track the highest slot index with data so culling only scans active slots.
      if (slotIndex + 1 > m_activeSlots) m_activeSlots.store(slotIndex + 1, std::memory_order_relaxed);
  }

  /// Rebuild indirect draw commands on the CPU from chunk metadata.
  /// Chunks are frustum-culled once and fanned out into opaque / transparent
  /// ranges so each draw pass only issues the commands it actually renders.
  void buildIndirectCommands(const Frustum& frustum) {
      const auto* chunks = static_cast<const ChunkCullData*>(m_chunkDataBuffer->mappedPtr());
      auto* commands = static_cast<DrawCommand*>(m_indirectBuffer->mappedPtr());

      uint32_t activeLimit = m_activeSlots.load(std::memory_order_relaxed);
      uint32_t terrainOpaqueCount = 0u;
      uint32_t terrainTransparentCount = 0u;
      for (uint32_t i = 0; i < activeLimit; ++i) {
        const auto& c = chunks[i];
        if (c.opaqueIndexCount == 0u && c.transparentIndexCount == 0u) continue;
        if (!frustum.intersectsAABB(c.min[0], c.min[1], c.min[2],
                                   c.max[0], c.max[1], c.max[2])) continue;
        
        if (c.opaqueIndexCount > 0u) {
          const DrawCommand cmd{c.opaqueIndexCount, 1u, c.opaqueFirstIndex, c.baseVertex, c.slotIndex};
          commands[terrainOpaqueCount++] = cmd;
        }
        if (c.transparentIndexCount > 0u) {
          const DrawCommand cmd{c.transparentIndexCount, 1u, c.transparentFirstIndex, c.baseVertex, c.slotIndex};
          commands[m_maxChunks + terrainTransparentCount++] = cmd;
        }
      }

      m_terrainOpaqueCount = terrainOpaqueCount;
      m_terrainTransparentCount = terrainTransparentCount;
      gl::MemoryBarrier(GL_COMMAND_BARRIER_BIT);
  }

  auto opaqueCommandCount() const -> uint32_t { return m_terrainOpaqueCount; }
  auto transparentCommandCount() const -> uint32_t { return m_terrainTransparentCount; }
  auto terrainOpaqueCommandCount() const -> uint32_t { return m_terrainOpaqueCount; }
  auto terrainTransparentCommandCount() const -> uint32_t { return m_terrainTransparentCount; }
  auto hasVisibleTransparent() const -> bool {
      return m_terrainTransparentCount > 0u;
  }

  /// Issue an indirect multi-draw for a range of commands.
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
  uint32_t m_terrainOpaqueCount = 0u;
  uint32_t m_terrainTransparentCount = 0u;
  std::atomic<uint32_t> m_activeSlots{0};
  std::unique_ptr<PersistentBuffer> m_chunkDataBuffer;
  std::unique_ptr<PersistentBuffer> m_indirectBuffer;
};

} // namespace terrain
