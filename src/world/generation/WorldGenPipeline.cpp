#include "WorldGenPipeline.hpp"
#include <algorithm>
#include <cstring>

namespace terrain {

WorldGenPipeline::WorldGenPipeline(uint32_t seed, const WorldGenerationConfig& config)
  : m_terrain(seed, config),
    m_densityNoise(seed)
{}

WorldGenPipeline::WorldGenPipeline(biome::IClimateSource& climateSource,
                                   uint32_t seed,
                                   const WorldGenerationConfig& config)
  : m_terrain(climateSource, seed, config),
    m_densityNoise(seed)
{}

auto WorldGenPipeline::sampleDensity(float worldX, float worldY, float worldZ) const -> float {
  return m_terrain.sampleDensity(worldX, worldY, worldZ);
}

auto WorldGenPipeline::sampleMaterial(float worldX, float worldY, float worldZ) const -> TerrainMaterial {
  return m_terrain.sampleMaterial(worldX, worldY, worldZ);
}

auto WorldGenPipeline::sampleMaterial(float worldX, float worldY, float worldZ,
                                      const glm::vec3& normal) const -> TerrainMaterial {
  return m_terrain.sampleMaterial(worldX, worldY, worldZ, normal);
}

} // namespace terrain
