#include "SharedPool.hpp"
#include <stdexcept>
#include <cstring>
#include <memory>

namespace voxel {

SharedPool::SharedPool(int32_t capacity, ChunkDimensions dims)
  : m_capacity(capacity), m_dims(dims) {
  computeLayout();
  m_buffer.resize(m_slotByteSize * capacity, 0);
  m_freeList.resize(capacity);
  for (int32_t i = 0; i < capacity; ++i) m_freeList[i] = i;
  m_freeHead = capacity;
}

void SharedPool::computeLayout() {
  m_voxelsBytes = static_cast<size_t>(m_dims.sizeX) * m_dims.sizeY * m_dims.sizeZ;
  m_lightBytes = m_voxelsBytes;
  m_redstoneBytes = m_voxelsBytes;
  m_vertsBytes = static_cast<size_t>(m_dims.maxVertsPerChunk) * m_dims.vertexStrideFloats * 4;
  m_indicesBytes = static_cast<size_t>(m_dims.maxIndicesPerChunk) * 4;

  size_t unaligned =
    m_headerBytes + m_voxelsBytes + m_lightBytes +
    m_redstoneBytes + m_vertsBytes + m_indicesBytes;
  m_slotByteSize = (unaligned + 63) & ~size_t{63};
}

auto SharedPool::create(int32_t capacity, ChunkDimensions dims) -> std::unique_ptr<SharedPool> {
  return std::unique_ptr<SharedPool>(new SharedPool(capacity, dims));
}

auto SharedPool::acquire() -> std::optional<ChunkSlot> {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_freeHead == 0) return std::nullopt;
  int32_t slotIndex = m_freeList[--m_freeHead];
  auto slot = view(slotIndex);
  // Initialize slot
  *slot.status = static_cast<int32_t>(ChunkSlotStatus::FREE);
  *slot.vertexCount = 0;
  *slot.indexCount = 0;
  std::memset(slot.voxels, 0, m_voxelsBytes);
  std::memset(slot.light, 0, m_lightBytes);
  std::memset(slot.redstone, 0, m_redstoneBytes);
  return slot;
}

void SharedPool::release(ChunkSlot slot) {
  std::lock_guard<std::mutex> lock(m_mutex);
  *slot.status = static_cast<int32_t>(ChunkSlotStatus::FREE);
  m_freeList[m_freeHead++] = slot.slotIndex;
}

void SharedPool::resize(int32_t newCapacity) {
  if (newCapacity <= m_capacity) return;

  // Allocate a new buffer large enough for the new capacity.
  std::vector<uint8_t> newBuf(m_slotByteSize * newCapacity, 0);

  // Copy existing slots to the new buffer (they stay at the same indices).
  std::memcpy(newBuf.data(), m_buffer.data(), m_buffer.size());

  // Swap the buffers.
  m_buffer.swap(newBuf);

  // Extend the free list with the new slot indices.
  m_freeList.resize(newCapacity);
  for (int32_t i = m_capacity; i < newCapacity; ++i) {
    m_freeList[m_freeHead++] = i;
  }
  m_capacity = newCapacity;
}

auto SharedPool::view(int32_t slotIndex) const -> ChunkSlot {
  size_t base = static_cast<size_t>(slotIndex) * m_slotByteSize;
  auto* buf = const_cast<uint8_t*>(m_buffer.data()); // non-const access to shared memory

  ChunkSlot slot{};
  slot.slotIndex = slotIndex;
  slot.buffer = buf;
  slot.baseByteOffset = base;
  slot.status = reinterpret_cast<int32_t*>(buf + base + 0);
  slot.vertexCount = reinterpret_cast<uint32_t*>(buf + base + 4);
  slot.indexCount = reinterpret_cast<uint32_t*>(buf + base + 8);
  slot.chunkX = reinterpret_cast<int32_t*>(buf + base + 12);
  slot.chunkZ = reinterpret_cast<int32_t*>(buf + base + 16);
  slot.genSeed = reinterpret_cast<uint32_t*>(buf + base + 20);
  slot.voxels = buf + base + m_headerBytes;
  slot.light = buf + base + m_headerBytes + m_voxelsBytes;
  slot.redstone = buf + base + m_headerBytes + m_voxelsBytes + m_lightBytes;
  slot.vertices = reinterpret_cast<float*>(
    buf + base + m_headerBytes + m_voxelsBytes + m_lightBytes + m_redstoneBytes);
  slot.indices = reinterpret_cast<uint32_t*>(
    buf + base + m_headerBytes + m_voxelsBytes + m_lightBytes + m_redstoneBytes + m_vertsBytes);
  return slot;
}

} // namespace voxel
