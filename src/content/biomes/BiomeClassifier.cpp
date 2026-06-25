#include "BiomeClassifier.hpp"
#include <algorithm>

namespace voxel::biome {

float BiomeClassifier::smoothEdge(float value, float threshold, float width) {
  float edge = (value - threshold + width * 0.5f) / width;
  edge = std::clamp(edge, 0.0f, 1.0f);
  return edge * edge * (3.0f - 2.0f * edge); // Hermite smoothstep
}

auto BiomeClassifier::pick(float temperature, float humidity) -> const BiomeSurfaceRule& {
  if (temperature > kDesertTempThreshold  && humidity < kDesertHumidThreshold) return DesertBiome;
  if (humidity    > kSwampHumidThreshold  && temperature > kSwampTempThreshold) return SwampBiome;
  if (temperature < kMountainTempThreshold)                                      return MountainsBiome;
  if (humidity    > kForestHumidThreshold)                                       return ForestBiome;
  return PlainsBiome;
}

auto BiomeClassifier::sampleBiome(const ClimateSample& c) -> const BiomeSurfaceRule& {
  return pick(c.temperature, c.humidity);
}

float BiomeClassifier::mountainWeight(const ClimateSample& c) {
  float w = 1.0f - smoothEdge(c.temperature, kMountainTempThreshold, kMountainTransWidth);
  return std::max(0.0f, w);
}

float BiomeClassifier::blendedHeightBias(const ClimateSample& c) {
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

} // namespace voxel::biome
