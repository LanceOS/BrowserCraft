#include "BiomeSampler.hpp"

namespace voxel::biome {

BiomeSampler::BiomeSampler(uint32_t seed)
  : m_tempNoise(seed ^ 0xa10beu),
    m_humidNoise(seed ^ 0xb1d07u)
{}

auto BiomeSampler::sampleTemperature(float worldX, float worldZ) const -> float {
  float raw = m_tempNoise.noise3D(worldX * kClimateScale, 0.0f, worldZ * kClimateScale);
  return (raw + 1.0f) * 0.5f;
}

auto BiomeSampler::sampleHumidity(float worldX, float worldZ) const -> float {
  float raw = m_humidNoise.noise3D(worldX * kClimateScale, 100.0f, worldZ * kClimateScale);
  return (raw + 1.0f) * 0.5f;
}

auto BiomeSampler::sampleClimate(float worldX, float worldZ) const -> ClimateSample {
  return {sampleTemperature(worldX, worldZ), sampleHumidity(worldX, worldZ)};
}

auto BiomeSampler::sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule& {
  return BiomeClassifier::sampleBiome(sampleClimate(worldX, worldZ));
}

auto BiomeSampler::mountainWeight(float worldX, float worldZ) const -> float {
  return BiomeClassifier::mountainWeight(sampleClimate(worldX, worldZ));
}

auto BiomeSampler::blendedHeightBias(float worldX, float worldZ) const -> float {
  return BiomeClassifier::blendedHeightBias(sampleClimate(worldX, worldZ));
}

} // namespace voxel::biome
