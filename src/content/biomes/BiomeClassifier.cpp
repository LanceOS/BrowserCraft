#include "BiomeClassifier.hpp"

namespace voxel::biome {

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
  if (w < 0.0f) w = 0.0f;
  return w;
}

auto BiomeClassifier::computeWeights(const ClimateSample& c) -> BiomeWeights {
  float t = c.temperature;
  float h = c.humidity;

  BiomeWeights w;
  w.mountains = 1.0f - smoothEdge(t, kMountainTempThreshold, kMountainTransWidth);
  w.desert    = smoothEdge(t, kDesertTempThreshold,  kDesertTransWidth)
              * (1.0f - smoothEdge(h, kDesertHumidThreshold, kDesertTransWidth));
  w.swamp     = smoothEdge(h, kSwampHumidThreshold, kSwampHumidTransWidth)
              * smoothEdge(t, kSwampTempThreshold,  kSwampTempTransWidth);
  // Forest suppressed where mountains or swamp would take priority (mirrors pick() ordering).
  w.forest    = smoothEdge(h, kForestHumidThreshold, kForestTransWidth)
              * (1.0f - w.mountains) * (1.0f - w.swamp);
  w.plains    = 1.0f - w.mountains - w.desert - w.swamp - w.forest;
  if (w.plains < 0.0f) w.plains = 0.0f;

  // Normalise so weights sum to 1.
  float total = w.mountains + w.desert + w.swamp + w.forest + w.plains;
  if (total > 0.0f) {
    w.mountains /= total;
    w.desert    /= total;
    w.swamp     /= total;
    w.forest    /= total;
    w.plains    /= total;
  }

  return w;
}

float BiomeClassifier::blendedHeightBias(const ClimateSample& c) {
  auto w = computeWeights(c);
  return
    w.mountains * MountainsBiome.heightBias +
    w.desert    * DesertBiome.heightBias +
    w.swamp     * SwampBiome.heightBias +
    w.forest    * ForestBiome.heightBias +
    w.plains    * PlainsBiome.heightBias;
}

} // namespace voxel::biome
