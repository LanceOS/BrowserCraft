#include "BiomeSampler.hpp"

namespace voxel::biome {

BiomeSampler::BiomeSampler(uint32_t seed)
  : m_tempNoise(seed ^ 0xa10beu), m_humidNoise(seed ^ 0xb1d07u) {}

auto BiomeSampler::noise2D(float worldX, float worldZ) const -> float {
  return m_tempNoise.noise3D(worldX, 0.0f, worldZ);
}

auto BiomeSampler::sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule& {
  float temperature = (m_tempNoise.noise3D(worldX * 0.008f, 0.0f, worldZ * 0.008f) + 1.0f) * 0.5f;
  float humidity = (m_humidNoise.noise3D(worldX * 0.008f, 100.0f, worldZ * 0.008f) + 1.0f) * 0.5f;
  return pick(temperature, humidity);
}

auto BiomeSampler::pick(float temperature, float humidity) -> const BiomeSurfaceRule& {
  if (temperature > 0.65f && humidity < 0.35f) return DesertBiome;
  if (humidity > 0.72f && temperature > 0.35f)  return SwampBiome;
  if (temperature < 0.28f)                        return MountainsBiome;
  if (humidity > 0.55f)                           return ForestBiome;
  return PlainsBiome;
}

} // namespace voxel::biome
