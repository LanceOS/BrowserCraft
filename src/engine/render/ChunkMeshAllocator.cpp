#include "ChunkMeshAllocator.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace terrain {

namespace {
// Smooth terrain meshes are denser than the old blocky terrain surfaces, so the
// shared arena needs a little more breathing room to avoid churn at runtime.
constexpr float kMeshBudgetFactor = 0.40f;
constexpr size_t kMinVboBudgetBytes = 1u << 20; // 1 MiB
constexpr size_t kMinIboBudgetBytes = 1u << 20; // 1 MiB
}

ChunkMeshAllocator::ChunkMeshAllocator(const GameConfig& config, int32_t maxSlots)
  : m_maxSlots(std::max(0, maxSlots)),
    m_vertexBytes(static_cast<size_t>(std::max(1, config.vertexStrideFloats)) * sizeof(float)),
    m_vboCapacityBytes([&] {
      const size_t worst = static_cast<size_t>(std::max(0, maxSlots))
        * static_cast<size_t>(std::max(0, config.maxVertsPerChunk))
        * static_cast<size_t>(std::max(1, config.vertexStrideFloats))
        * sizeof(float);
      const size_t budget = std::max(kMinVboBudgetBytes, static_cast<size_t>(static_cast<float>(worst) * kMeshBudgetFactor));
      return alignUp(budget, std::max<size_t>(m_vertexBytes, 4u));
    }()),
    m_iboCapacityBytes([&] {
      const size_t worst = static_cast<size_t>(std::max(0, maxSlots))
        * static_cast<size_t>(std::max(0, config.maxIndicesPerChunk))
        * sizeof(uint32_t);
      const size_t budget = std::max(kMinIboBudgetBytes, static_cast<size_t>(static_cast<float>(worst) * kMeshBudgetFactor));
      return alignUp(budget, alignof(uint32_t));
    }()),
    m_vbo(m_vboCapacityBytes, GL_ARRAY_BUFFER),
    m_ibo(m_iboCapacityBytes, GL_ELEMENT_ARRAY_BUFFER)
{
  if (m_vertexBytes == 0) {
    throw std::runtime_error("ChunkMeshAllocator requires a positive vertex stride");
  }

  m_freeVbo.emplace(0, m_vboCapacityBytes);
  m_freeIbo.emplace(0, m_iboCapacityBytes);

  std::memset(m_vbo.mappedPtr(), 0, m_vbo.capacity());
  std::memset(m_ibo.mappedPtr(), 0, m_ibo.capacity());
}

auto ChunkMeshAllocator::alignUp(size_t value, size_t alignment) -> size_t {
  if (alignment <= 1u) return value;
  const size_t remainder = value % alignment;
  return remainder == 0u ? value : value + (alignment - remainder);
}

auto ChunkMeshAllocator::allocateSpan(std::map<size_t, size_t>& freeList,
                                      size_t bytes,
                                      size_t alignment) -> std::optional<size_t> {
  if (bytes == 0) return size_t{0};
  if (alignment == 0) alignment = 1;

  for (auto it = freeList.begin(); it != freeList.end(); ++it) {
    const size_t spanOffset = it->first;
    const size_t spanBytes = it->second;
    const size_t alignedOffset = alignUp(spanOffset, alignment);
    if (alignedOffset < spanOffset) continue;
    const size_t padding = alignedOffset - spanOffset;
    if (padding > spanBytes) continue;
    if (bytes > spanBytes - padding) continue;

    const size_t tailOffset = alignedOffset + bytes;
    const size_t tailBytes = spanBytes - padding - bytes;
    const size_t headBytes = padding;

    freeList.erase(it);
    if (headBytes > 0) {
      freeList.emplace(spanOffset, headBytes);
    }
    if (tailBytes > 0) {
      freeList.emplace(tailOffset, tailBytes);
    }
    return alignedOffset;
  }

  return std::nullopt;
}

void ChunkMeshAllocator::freeSpan(std::map<size_t, size_t>& freeList, size_t offset, size_t bytes) {
  if (bytes == 0) return;

  size_t start = offset;
  size_t size = bytes;

  auto it = freeList.lower_bound(start);
  if (it != freeList.begin()) {
    auto prev = std::prev(it);
    if (prev->first + prev->second == start) {
      start = prev->first;
      size += prev->second;
      freeList.erase(prev);
    }
  }

  it = freeList.lower_bound(start);
  if (it != freeList.end() && start + size == it->first) {
    size += it->second;
    freeList.erase(it);
  }

  freeList.emplace(start, size);
}

auto ChunkMeshAllocator::consumeSpan(std::map<size_t, size_t>& freeList,
                                     size_t offset,
                                     size_t bytes) -> bool {
  if (bytes == 0) return true;
  if (bytes > std::numeric_limits<size_t>::max() - offset) return false;

  const size_t end = offset + bytes;
  auto it = freeList.upper_bound(offset);
  if (it == freeList.begin()) return false;
  --it;

  const size_t spanOffset = it->first;
  const size_t spanBytes = it->second;
  const size_t spanEnd = spanOffset + spanBytes;
  if (offset < spanOffset || end > spanEnd) return false;

  const size_t headBytes = offset - spanOffset;
  const size_t tailBytes = spanEnd - end;
  freeList.erase(it);
  if (headBytes > 0) {
    freeList.emplace(spanOffset, headBytes);
  }
  if (tailBytes > 0) {
    freeList.emplace(end, tailBytes);
  }
  return true;
}

auto ChunkMeshAllocator::allocationLocked(int32_t slotIndex) const -> std::optional<MeshAllocation> {
  auto it = m_allocations.find(slotIndex);
  if (it == m_allocations.end()) return std::nullopt;
  return it->second;
}

auto ChunkMeshAllocator::allocateForSlot(int32_t slotIndex,
                                         size_t vboBytes,
                                         size_t iboBytes) -> std::optional<MeshAllocation> {
  std::lock_guard lock(m_mutex);
  vboBytes = alignUp(vboBytes, m_vertexBytes);
  iboBytes = alignUp(iboBytes, alignof(uint32_t));
  if (vboBytes == 0 || iboBytes == 0) {
    return std::nullopt;
  }

  const auto existing = allocationLocked(slotIndex);

  auto allocateFresh = [&]() -> std::optional<MeshAllocation> {
    auto vboOffset = allocateSpan(m_freeVbo, vboBytes, m_vertexBytes);
    if (!vboOffset) {
      return std::nullopt;
    }

    auto iboOffset = allocateSpan(m_freeIbo, iboBytes, alignof(uint32_t));
    if (!iboOffset) {
      freeSpan(m_freeVbo, *vboOffset, vboBytes);
      return std::nullopt;
    }

    MeshAllocation alloc{};
    alloc.slotIndex = slotIndex;
    alloc.vboOffsetBytes = *vboOffset;
    alloc.iboOffsetBytes = *iboOffset;
    alloc.vboBytes = vboBytes;
    alloc.iboBytes = iboBytes;
    alloc.baseVertex = static_cast<int32_t>(alloc.vboOffsetBytes / m_vertexBytes);
    alloc.vboPtr = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(m_vbo.mappedPtr()) + alloc.vboOffsetBytes);
    alloc.iboPtr = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(m_ibo.mappedPtr()) + alloc.iboOffsetBytes);
    return alloc;
  };

  auto commitAllocation = [&](const MeshAllocation& alloc) -> std::optional<MeshAllocation> {
    releaseSlotLocked(slotIndex);
    m_allocations.emplace(slotIndex, alloc);
    return alloc;
  };

  if (auto alloc = allocateFresh()) {
    return commitAllocation(*alloc);
  }

  if (!existing) {
    return std::nullopt;
  }

  releaseSlotLocked(slotIndex);

  if (auto alloc = allocateFresh()) {
    return commitAllocation(*alloc);
  }

  const bool restoredVbo = consumeSpan(m_freeVbo, existing->vboOffsetBytes, existing->vboBytes);
  const bool restoredIbo = consumeSpan(m_freeIbo, existing->iboOffsetBytes, existing->iboBytes);
  if (!restoredVbo || !restoredIbo) {
    throw std::runtime_error("ChunkMeshAllocator failed to restore a previous allocation");
  }
  m_allocations.emplace(slotIndex, *existing);
  return std::nullopt;
}

void ChunkMeshAllocator::releaseSlot(int32_t slotIndex) {
  std::lock_guard lock(m_mutex);
  releaseSlotLocked(slotIndex);
}

void ChunkMeshAllocator::releaseSlotLocked(int32_t slotIndex) {
  auto it = m_allocations.find(slotIndex);
  if (it == m_allocations.end()) return;

  freeSpan(m_freeVbo, it->second.vboOffsetBytes, it->second.vboBytes);
  freeSpan(m_freeIbo, it->second.iboOffsetBytes, it->second.iboBytes);
  m_allocations.erase(it);
}

auto ChunkMeshAllocator::allocationForSlot(int32_t slotIndex) const -> std::optional<MeshAllocation> {
  std::lock_guard lock(m_mutex);
  return allocationLocked(slotIndex);
}

} // namespace terrain
