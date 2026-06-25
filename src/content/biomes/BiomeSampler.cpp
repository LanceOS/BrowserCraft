#include "BiomeSampler.hpp"
#include <algorithm>
#include <cmath>

namespace voxel::biome {

// ---------------------------------------------------------------------------
// Hermite smoothstep helper — captureless, reusable.
// Maps [threshold - width/2, threshold + width/2] → [0, 1] with smooth
// ease-in-out.  Values outside the transition range clamp to 0 or 1.
// ---------------------------------------------------------------------------
static float smoothEdge(float value, float threshold, float width) {
  float edge = (value - threshold + width * 0.5f) / width;
  edge = std::clamp(edge, 0.0f, 1.0f);
  return edge * edge * (3.0f - 2.0f * edge); // Hermite smoothstep
}

BiomeSampler::BiomeSampler(uint32_t seed)
  : m_tempNoise(seed ^ 0xa10beu),
    m_humidNoise(seed ^ 0xb1d07u)
{}

auto BiomeSampler::sampleTemperature(float worldX, float worldZ) const -> float {
  return (m_tempNoise.noise3D(worldX * kClimateScale, 0.0f, worldZ * kClimateScale) + 1.0f) * 0.5f;
}

auto BiomeSampler::sampleHumidity(float worldX, float worldZ) const -> float {
  return (m_humidNoise.noise3D(worldX * kClimateScale, 100.0f, worldZ * kClimateScale) + 1.0f) * 0.5f;
}

auto BiomeSampler::sampleClimate(float worldX, float worldZ) const -> ClimateSample {
  return {sampleTemperature(worldX, worldZ), sampleHumidity(worldX, worldZ)};
}

// ---- World-coordinate versions (convenience — call sampleClimate internally) ----

auto BiomeSampler::sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule& {
  return sampleBiome(sampleClimate(worldX, worldZ));
}

auto BiomeSampler::mountainWeight(float worldX, float worldZ) const -> float {
  return mountainWeight(sampleClimate(worldX, worldZ));
}

auto BiomeSampler::blendedHeightBias(float worldX, float worldZ) const -> float {
  return blendedHeightBias(sampleClimate(worldX, worldZ));
}

// ---- ClimateSample-based overloads (pure functions, no noise) ----

auto BiomeSampler::sampleBiome(const ClimateSample& c) -> const BiomeSurfaceRule& {
  return pick(c.temperature, c.humidity);
}

auto BiomeSampler::mountainWeight(const ClimateSample& c) -> float {
  float w = 1.0f - smoothEdge(c.temperature, kMountainTempThreshold, kMountainTransWidth);
  return std::max(0.0f, w);
}

auto BiomeSampler::blendedHeightBias(const ClimateSample& c) -> float {
  float t = c.temperature;
  float h = c.humidity;

  // Weight each biome using smoothstep transitions aligned with pick().
  float wMountains = 1.0f - smoothEdge(t, kMountainTempThreshold, kMountainTransWidth);
  float wDesert    = smoothEdge(t, kDesertTempThreshold,  kDesertTransWidth)
                   * (1.0f - smoothEdge(h, kDesertHumidThreshold, kDesertTransWidth));
  float wSwamp     = smoothEdge(h, kSwampHumidThreshold, kSwampHumidTransWidth)
                   * smoothEdge(t, kSwampTempThreshold,  kSwampTempTransWidth);
  // Forest suppressed where mountains or swamp would take priority (mirrors pick() ordering).
  float wForest    = smoothEdge(h, kForestHumidThreshold, kForestTransWidth)
                   * (1.0f - wMountains) * (1.0f - wSwamp);
  float wPlains    = std::max(0.0f, 1.0f - wMountains - wDesert - wSwamp - wForest);

  float total = wMountains + wDesert + wSwamp + wForest + wPlains;
  if (total <= 0.0f) return PlainsBiome.heightBias;

  return
    (wMountains / total) * MountainsBiome.heightBias +
    (wDesert    / total) * DesertBiome.heightBias +
    (wSwamp     / total) * SwampBiome.heightBias +
    (wForest    / total) * ForestBiome.heightBias +
    (wPlains    / total) * PlainsBiome.heightBias;
}

// ---- Static helpers ----

auto BiomeSampler::pick(float temperature, float humidity) -> const BiomeSurfaceRule& {
  if (temperature > kDesertTempThreshold  && humidity < kDesertHumidThreshold) return DesertBiome;
  if (humidity    > kSwampHumidThreshold  && temperature > kSwampTempThreshold) return SwampBiome;
  if (temperature < kMountainTempThreshold)                                      return MountainsBiome;
  if (humidity    > kForestHumidThreshold)                                       return ForestBiome;
  return PlainsBiome;
}

} // namespace voxel::biome
