#include "WorldGenPipeline.hpp"

#include "world/BlockIds.hpp"

#include <algorithm>
#include <cstring>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

WorldGenPipeline::WorldGenPipeline(uint32_t seed, const WorldGenerationConfig& config)
  : m_terrain(seed, config),
    m_densityNoise(seed),
    m_caveCarver(seed),
    m_oreDist(seed)
{}

WorldGenPipeline::WorldGenPipeline(biome::IClimateSource& climateSource,
                                   uint32_t seed,
                                   const WorldGenerationConfig& config)
  : m_terrain(climateSource, seed, config),
    m_densityNoise(seed),
    m_caveCarver(seed),
    m_oreDist(seed)
{}

void WorldGenPipeline::generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                                 int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                                 uint32_t chunkSeed) {
  const int32_t baseX = chunkX * sizeX;
  const int32_t baseZ = chunkZ * sizeZ;
  const auto& cfg = m_terrain.config();
  const int32_t terrainMaxY = sizeY - 2;

  for (int32_t z = 0; z < sizeZ; ++z) {
    for (int32_t x = 0; x < sizeX; ++x) {
      const float worldX = static_cast<float>(baseX + x);
      const float worldZ = static_cast<float>(baseZ + z);

      const auto terrain = m_terrain.sampleTerrain(worldX, worldZ);
      const auto* activeBiome = terrain.biome;
      const int32_t clampedSurfaceY = std::clamp(terrain.surfaceY, 1, terrainMaxY);
      const bool noWater = terrain.noWater;

      for (int32_t y = 0; y < sizeY; ++y) {
        const int32_t index = (y * sizeZ + z) * sizeX + x;

        if (y == 0) {
          voxels[index] = BlockId::BEDROCK;
          continue;
        }

        // Above surface
        if (y > clampedSurfaceY) {
          voxels[index] = (y <= cfg.seaLevel && !noWater) ? BlockId::WATER : BlockId::AIR;
          continue;
        }

        // ---- 3D density carve-out (underground cavities / overhangs) ----
        // The threshold increases with depth so cavities only form well
        // below the surface, preventing jagged surface terrain.
        float depthFactor = static_cast<float>(clampedSurfaceY - y) * cfg.densityDepthScale;
        float noise3D = m_densityNoise.noise3D(
            worldX * cfg.densityNoiseScale,
            static_cast<float>(y) * cfg.densityNoiseScale,
            worldZ * cfg.densityNoiseScale);

        // Only carve when we're at least 4 blocks below the surface.
        if (noise3D + depthFactor < 0.0f && y < clampedSurfaceY - 4) {
          voxels[index] = 0; // air pocket
          continue;
        }

        // ---- Surface layering ----
        if (y == clampedSurfaceY) {
          voxels[index] = activeBiome ? activeBiome->topBlock() : BlockId::STONE;
        } else if (activeBiome && y > clampedSurfaceY - activeBiome->surfaceDepth()) {
          voxels[index] = activeBiome->fillerBlock();
        } else {
          voxels[index] = BlockId::STONE;
        }
      }
    }
  }

  // ---- Post-processing ----
  m_caveCarver.carve(voxels, baseX, baseZ, sizeX, sizeY, sizeZ, chunkSeed);
  m_oreDist.distribute(voxels, sizeX, sizeY, sizeZ, chunkSeed);
}

auto WorldGenPipeline::sampleDensity(float worldX, float worldY, float worldZ) const -> float {
  return m_terrain.sampleDensity(worldX, worldY, worldZ);
}

auto WorldGenPipeline::sampleMaterial(float worldX, float worldY, float worldZ) const -> MaterialId {
  return m_terrain.sampleMaterial(worldX, worldY, worldZ);
}

void WorldGenPipeline::fillChunk(uint8_t* voxels, int32_t* chunkXPtr, int32_t* chunkZPtr,
                                  uint32_t* genSeed, int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  int32_t cx = *chunkXPtr;
  int32_t cz = *chunkZPtr;
  generate(voxels, cx, cz, sizeX, sizeY, sizeZ, genSeed ? *genSeed : 0);
}

} // namespace voxel
