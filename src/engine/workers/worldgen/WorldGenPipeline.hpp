#pragma once

#include <cstdint>
#include "SimplexNoise.hpp"
#include "../../../content/biomes/BiomeSampler.hpp"
#include "CaveCarver.hpp"
#include "OreDistributor.hpp"

namespace voxel {

/// Full world generation pipeline for a single chunk.
/// Call generate() with a ChunkSlot to fill its voxels, light, and redstone arrays.
class WorldGenPipeline {
public:
  WorldGenPipeline(uint32_t seed);

  /// Generate terrain into the given voxel array.
  void generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                int32_t sizeX, int32_t sizeY, int32_t sizeZ);

  /// Fill a chunk from an acquired slot (used by worker threads).
  void fillChunk(uint8_t* voxels, int32_t* chunkXPtr, int32_t* chunkZPtr,
                 uint32_t* genSeed, int32_t sizeX, int32_t sizeY, int32_t sizeZ);

private:
  static constexpr uint8_t STONE = 1;
  static constexpr uint8_t DIRT = 3;
  static constexpr uint8_t BEDROCK = 7;
  static constexpr uint8_t WATER = 8;

  SimplexNoise m_densityNoise;
  biome::BiomeSampler m_biomeSampler;
  CaveCarver m_caveCarver;
  OreDistributor m_oreDist;
};

} // namespace voxel
