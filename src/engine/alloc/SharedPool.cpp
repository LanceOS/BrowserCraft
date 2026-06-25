#include "SharedPool.hpp"
#include <stdexcept>
#include <cstring>
#include <memory>

namespace voxel {

namespace {

constexpr size_t kStatusOffset = 0;
constexpr size_t kVertexCountOffset = 4;
constexpr size_t kIndexCountOffset = 8;
constexpr size_t kChunkXOffset = 12;
constexpr size_t kChunkZOffset = 16;
constexpr size_t kGenSeedOffset = 20;
constexpr size_t kRenderFlagsOffset = 24;

} // namespace

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
  // Vertex and index buffers removed — mesher writes directly to GPU VBO/IBO
  m_vertsBytes = 0;
  m_indicesBytes = 0;

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
  *slot.renderFlags = 0u;
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
  *slot.renderFlags = 0u;
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
  slot.status = reinterpret_cast<int32_t*>(buf + base + kStatusOffset);
  slot.renderFlags = reinterpret_cast<uint32_t*>(buf + base + kRenderFlagsOffset);
  slot.vertexCount = reinterpret_cast<uint32_t*>(buf + base + kVertexCountOffset);
  slot.indexCount = reinterpret_cast<uint32_t*>(buf + base + kIndexCountOffset);
  slot.chunkX = reinterpret_cast<int32_t*>(buf + base + kChunkXOffset);
  slot.chunkZ = reinterpret_cast<int32_t*>(buf + base + kChunkZOffset);
  slot.genSeed = reinterpret_cast<uint32_t*>(buf + base + kGenSeedOffset);
  slot.voxels = buf + base + m_headerBytes;
  slot.light = buf + base + m_headerBytes + m_voxelsBytes;
  slot.redstone = buf + base + m_headerBytes + m_voxelsBytes + m_lightBytes;
  // slot.vertices and slot.indices removed — mesher writes directly to GPU.
  return slot;
}

} // namespace voxel
