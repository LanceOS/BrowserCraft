#include "WorldGenPipeline.hpp"
#include <cmath>
#include <cstring>

namespace voxel {

WorldGenPipeline::WorldGenPipeline(uint32_t seed)
  : m_densityNoise(seed),
    m_biomeSampler(seed),
    m_caveCarver(seed),
    m_oreDist(seed)
{}

void WorldGenPipeline::generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                                 int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  int32_t baseX = chunkX * sizeX;
  int32_t baseZ = chunkZ * sizeZ;

  for (int32_t z = 0; z < sizeZ; ++z) {
    for (int32_t x = 0; x < sizeX; ++x) {
      float worldX = static_cast<float>(baseX + x);
      float worldZ = static_cast<float>(baseZ + z);

      float heightMap = m_biomeSampler.noise2D(worldX * 0.01f, worldZ * 0.01f);
      const auto& rule = m_biomeSampler.sampleBiome(worldX, worldZ);
      int32_t baseHeight = static_cast<int32_t>(64.0f + heightMap * 16.0f + rule.heightBias);

      for (int32_t y = 0; y < sizeY; ++y) {
        int32_t index = (y * sizeZ + z) * sizeX + x;

        if (y == 0) {
          voxels[index] = BEDROCK;
          continue;
        }

        if (y > baseHeight) {
          voxels[index] = (y <= 64 && rule.name != "desert") ? WATER : 0;
          continue;
        }

        float depthFactor = static_cast<float>(baseHeight - y) * 0.05f;
        float noise3D = m_densityNoise.noise3D(worldX * 0.03f, static_cast<float>(y) * 0.03f, worldZ * 0.03f);

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
