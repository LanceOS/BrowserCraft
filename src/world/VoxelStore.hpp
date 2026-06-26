#pragma once

#include "ChunkManager.hpp"
#include <cstdint>

namespace voxel {

class BlockRegistry;
class SharedPool;
struct GameConfig;

/// Thin data-access layer for reading and writing block, light, and redstone
/// data in a world's voxel storage.  Wraps SharedPool + ChunkManager + 
/// BlockRegistry into a single interface so callers don't need to coordinate
/// all three manually.
class VoxelStore {
public:
  VoxelStore(SharedPool& pool, const BlockRegistry& blocks,
             const ChunkManager& chunks, const GameConfig& config);

  // ---- Block IDs --------------------------------------------------------
  [[nodiscard]] auto getBlockId(int32_t worldX, int32_t worldY,
                                int32_t worldZ) const -> uint8_t;
  void setBlockId(const Chunk& chunk, int32_t worldY, int32_t localX,
                  int32_t localZ, uint8_t blockId) const;

  // ---- Redstone ---------------------------------------------------------
  [[nodiscard]] auto getRedstone(const Chunk& chunk, int32_t worldY,
                                  int32_t localX,
                                  int32_t localZ) const -> uint8_t;
  void setRedstone(const Chunk& chunk, int32_t worldY, int32_t localX,
                   int32_t localZ, uint8_t packed) const;

  // ---- Properties -------------------------------------------------------
  [[nodiscard]] auto isSolid(int32_t worldX, int32_t worldY,
                              int32_t worldZ) const -> bool;
  [[nodiscard]] auto isSolidInChunk(int32_t worldX, int32_t worldY,
                                     int32_t worldZ,
                                     const Chunk& chunk) const -> bool;
  [[nodiscard]] auto isFluid(int32_t worldX, int32_t worldY,
                              int32_t worldZ) const -> bool;

private:
  SharedPool& m_pool;
  const BlockRegistry& m_blocks;
  const ChunkManager& m_chunks;
  const GameConfig& m_config;
};

} // namespace voxel
