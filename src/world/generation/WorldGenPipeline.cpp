#include "WorldGenPipeline.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace voxel {

WorldGenPipeline::WorldGenPipeline(uint32_t seed, const WorldGenerationConfig& config)
  : m_densityNoise(seed),
    m_biomeSampler(seed),
    m_caveCarver(seed),
    m_oreDist(seed),
    m_config(config)
{}

void WorldGenPipeline::generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                                 int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  int32_t baseX = chunkX * sizeX;
  int32_t baseZ = chunkZ * sizeZ;

  for (int32_t z = 0; z < sizeZ; ++z) {
    for (int32_t x = 0; x < sizeX; ++x) {
      float worldX = static_cast<float>(baseX + x);
      float worldZ = static_cast<float>(baseZ + z);

      const auto& settings = m_config;
      float biomeHeight = m_biomeSampler.noise2D(worldX * settings.baseHeightScale,
                                                worldZ * settings.baseHeightScale);
      float detailHeight = m_densityNoise.noise2D(worldX * settings.detailHeightScale,
                                                 worldZ * settings.detailHeightScale);
      const auto& rule = m_biomeSampler.sampleBiome(worldX, worldZ);
      int32_t baseHeight = static_cast<int32_t>(
        settings.baseHeight + biomeHeight * settings.baseHeightAmplitude + detailHeight * settings.detailHeightAmplitude + rule.heightBias);
      baseHeight = std::clamp(baseHeight, 1, sizeY - 2);
      int32_t seaLevel = settings.seaLevel;

      for (int32_t y = 0; y < sizeY; ++y) {
        int32_t index = (y * sizeZ + z) * sizeX + x;

        if (y == 0) {
          voxels[index] = BEDROCK;
          continue;
        }

        if (y > baseHeight) {
          voxels[index] = (y <= seaLevel && rule.name != "desert") ? WATER : 0;
          continue;
        }

        float depthFactor = static_cast<float>(baseHeight - y) * settings.densityDepthScale;
        float noise3D = m_densityNoise.noise3D(
          worldX * settings.densityNoiseScale,
          static_cast<float>(y) * settings.densityNoiseScale,
          worldZ * settings.densityNoiseScale);

        if (noise3D + depthFactor < 0.0f && y < baseHeight - 5) {
          voxels[index] = 0; // air pocket
          continue;
        }

        if (y == baseHeight) {
          voxels[index] = rule.topBlock;
        } else if (y > baseHeight - rule.depth) {
          voxels[index] = rule.fillerBlock;
        } else {
          voxels[index] = STONE;
        }
      }
    }
  }

  // Post-processing
  m_caveCarver.carve(voxels, baseX, baseZ, sizeX, sizeY, sizeZ);
  m_oreDist.distribute(voxels, sizeX, sizeY, sizeZ);
}

void WorldGenPipeline::fillChunk(uint8_t* voxels, int32_t* chunkXPtr, int32_t* chunkZPtr,
                                  uint32_t* genSeed, int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  int32_t cx = *chunkXPtr;
  int32_t cz = *chunkZPtr;
  *genSeed = 0;
  generate(voxels, cx, cz, sizeX, sizeY, sizeZ);
}

} // namespace voxel
