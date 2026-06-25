#include "BiomeFactory.hpp"

namespace voxel::biome {

const Biome& BiomeFactory::forId(BiomeId id) {
  switch (id) {
    case BiomeId::Plains:    return PlainsBiome::instance();
    case BiomeId::Desert:    return DesertBiome::instance();
    case BiomeId::Forest:    return ForestBiome::instance();
    case BiomeId::Mountains: return MountainsBiome::instance();
    case BiomeId::Swamp:     return SwampBiome::instance();
    case BiomeId::Ocean:     return OceanBiome::instance();
  }
  return PlainsBiome::instance(); // fallback
}

const Biome& BiomeFactory::pick(float temperature, float humidity) {
  if (temperature > kDesertTempThreshold  && humidity < kDesertHumidThreshold) return DesertBiome::instance();
  if (humidity    > kSwampHumidThreshold  && temperature > kSwampTempThreshold) return SwampBiome::instance();
  if (temperature < kMountainTempThreshold)                                      return MountainsBiome::instance();
  if (humidity    > kForestHumidThreshold)                                       return ForestBiome::instance();
  if (temperature < kOceanTempThreshold  && humidity < kOceanHumidThreshold)      return OceanBiome::instance();
  return PlainsBiome::instance();
}

const Biome& BiomeFactory::pick(const ClimateSample& c) {
  return pick(c.temperature, c.humidity);
}

float BiomeFactory::mountainWeight(const ClimateSample& c) {
  float w = 1.0f - smoothEdge(c.temperature, kMountainTempThreshold, kMountainTransWidth);
  if (w < 0.0f) w = 0.0f;
  return w;
}

auto BiomeFactory::computeWeights(const ClimateSample& c) -> BiomeWeights {
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
  // Ocean: cool-ish and dry-ish, suppressed where mountains dominate.
  w.ocean     = (1.0f - smoothEdge(t, kOceanTempThreshold,  kOceanTransWidth))
              * (1.0f - smoothEdge(h, kOceanHumidThreshold, kOceanTransWidth))
              * (1.0f - w.mountains);
  // Plains: remainder.
  w.plains    = 1.0f - w.mountains - w.desert - w.swamp - w.forest - w.ocean;
  if (w.plains < 0.0f) w.plains = 0.0f;

  // Normalise so weights sum to 1.
  float total = w.mountains + w.desert + w.swamp + w.forest + w.ocean + w.plains;
  if (total > 0.0f) {
    w.mountains /= total;
    w.desert    /= total;
    w.swamp     /= total;
    w.forest    /= total;
    w.ocean     /= total;
    w.plains    /= total;
  }

  return w;
}

float BiomeFactory::blendedHeightBias(const ClimateSample& c) {
  auto w = computeWeights(c);
  return
    w.mountains * MountainsBiome::instance().heightBias() +
    w.desert    * DesertBiome::instance().heightBias() +
    w.swamp     * SwampBiome::instance().heightBias() +
    w.forest    * ForestBiome::instance().heightBias() +
    w.ocean     * OceanBiome::instance().heightBias() +
    w.plains    * PlainsBiome::instance().heightBias();
}

} // namespace voxel::biome
