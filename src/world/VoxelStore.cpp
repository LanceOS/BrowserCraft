#include "VoxelStore.hpp"
#include "ChunkCoords.hpp"
#include "Chunk.hpp"
#include "BlockRegistry.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include <cmath>

namespace voxel {

VoxelStore::VoxelStore(SharedPool& pool, const BlockRegistry& blocks,
                       const ChunkManager& chunks, const GameConfig& config)
  : m_pool(pool), m_blocks(blocks), m_chunks(chunks), m_config(config)
{}

// ---- Block IDs ---------------------------------------------------------

auto VoxelStore::getBlockId(int32_t worldX, int32_t worldY,
                            int32_t worldZ) const -> uint8_t
{
  if (worldY < 0 || worldY >= m_config.worldHeight) return 0;
  int32_t cx = worldToChunk(static_cast<float>(worldX), m_config.chunkSize);
  int32_t cz = worldToChunk(static_cast<float>(worldZ), m_config.chunkSize);
  const auto* chunk = m_chunks.get(cx, cz);
  if (!chunk) return 0;
  int32_t localX = mod(worldX, m_config.chunkSize);
  int32_t localZ = mod(worldZ, m_config.chunkSize);
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
  int32_t localX = mod(worldX, m_config.chunkSize);
  int32_t localZ = mod(worldZ, m_config.chunkSize);
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
