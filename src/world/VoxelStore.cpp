#include "VoxelStore.hpp"
#include "Chunk.hpp"
#include "BlockRegistry.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include <algorithm>
#include <cmath>

namespace voxel {

VoxelStore::VoxelStore(SharedPool& pool, const BlockRegistry& blocks,
                       const ChunkManager& chunks, const GameConfig& config)
  : m_pool(pool), m_blocks(blocks), m_chunks(chunks), m_config(config)
{}

// ---- Coordinate helpers ------------------------------------------------

auto VoxelStore::worldToChunk(float coord) const -> int32_t {
  return static_cast<int32_t>(
      std::floor(coord / static_cast<float>(m_config.chunkSize)));
}

auto VoxelStore::mod(int32_t value) const -> int32_t {
  int32_t r = value % m_config.chunkSize;
  return r < 0 ? r + m_config.chunkSize : r;
}

auto VoxelStore::chunkSize() const -> int32_t { return m_config.chunkSize; }
auto VoxelStore::worldHeight() const -> int32_t { return m_config.worldHeight; }

// ---- Internal: resolve chunk + slot for a world coordinate --------------

static inline auto slotVoxels(SharedPool& pool, int32_t slotIndex) -> ChunkSlot {
  return pool.view(slotIndex);
}

// ---- Block IDs ---------------------------------------------------------

auto VoxelStore::getBlockId(int32_t worldX, int32_t worldY,
                            int32_t worldZ) const -> uint8_t
{
  if (worldY < 0 || worldY >= m_config.worldHeight) return 0;
  int32_t cx = worldToChunk(static_cast<float>(worldX));
  int32_t cz = worldToChunk(static_cast<float>(worldZ));
  const auto* chunk = m_chunks.get(cx, cz);
  if (!chunk) return 0;
  int32_t localX = mod(worldX);
  int32_t localZ = mod(worldZ);
  auto slot = m_pool.view(chunk->slotIndex);
  return slot.voxels[(worldY * m_config.chunkSize + localZ) * m_config.chunkSize + localX];
}

void VoxelStore::setBlockId(const Chunk& chunk, int32_t worldY,
                            int32_t localX, int32_t localZ,
                            uint8_t blockId) const
{
  if (worldY < 0 || worldY >= m_config.worldHeight) return;
  auto slot = m_pool.view(chunk.slotIndex);
  slot.voxels[(worldY * m_config.chunkSize + localZ) * m_config.chunkSize + localX] = blockId;
}

// ---- Redstone ----------------------------------------------------------

auto VoxelStore::getRedstone(const Chunk& chunk, int32_t worldY,
                              int32_t localX, int32_t localZ) const -> uint8_t
{
  if (worldY < 0 || worldY >= m_config.worldHeight) return 0;
  auto slot = m_pool.view(chunk.slotIndex);
  return slot.redstone[(worldY * m_config.chunkSize + localZ) * m_config.chunkSize + localX];
}

void VoxelStore::setRedstone(const Chunk& chunk, int32_t worldY,
                              int32_t localX, int32_t localZ,
                              uint8_t packed) const
{
  if (worldY < 0 || worldY >= m_config.worldHeight) return;
  auto slot = m_pool.view(chunk.slotIndex);
  slot.redstone[(worldY * m_config.chunkSize + localZ) * m_config.chunkSize + localX] = packed;
}

// ---- Properties --------------------------------------------------------

auto VoxelStore::isSolid(int32_t worldX, int32_t worldY,
                          int32_t worldZ) const -> bool
{
  uint8_t blockId = getBlockId(worldX, worldY, worldZ);
  if (blockId == 0) return false;
  const auto* def = m_blocks.tryGet(blockId);
  return def && def->collision.hasVolume();
}

auto VoxelStore::isSolidInChunk(int32_t worldX, int32_t worldY,
                                 int32_t worldZ,
                                 const Chunk& chunk) const -> bool
{
  if (worldY < 0 || worldY >= m_config.worldHeight) return false;
  auto slot = m_pool.view(chunk.slotIndex);
  int32_t localX = mod(worldX);
  int32_t localZ = mod(worldZ);
  uint8_t blockId = slot.voxels[(worldY * m_config.chunkSize + localZ) * m_config.chunkSize + localX];
  if (blockId == 0) return false;
  const auto* def = m_blocks.tryGet(blockId);
  return def && def->collision.hasVolume();
}

auto VoxelStore::isFluid(int32_t worldX, int32_t worldY,
                          int32_t worldZ) const -> bool
{
  uint8_t blockId = getBlockId(worldX, worldY, worldZ);
  if (blockId == 0) return false;
  const auto* def = m_blocks.tryGet(blockId);
  return def && def->material.liquid;
}

} // namespace voxel
