#include "BiomeFactory.hpp"

namespace voxel::biome {

namespace {

auto evaluateClimate(const ClimateSample& c) -> BiomeFactory::BiomeEvaluation {
  const float t = c.temperature;
  const float h = c.humidity;

  BiomeFactory::BiomeEvaluation result;

  const float mountainWeight = 1.0f - smoothEdge(t, kMountainTempThreshold, kMountainTransWidth);
  const float desertTemp = smoothEdge(t, kDesertTempThreshold, kDesertTransWidth);
  const float desertHumid = smoothEdge(h, kDesertHumidThreshold, kDesertTransWidth);
  const float swampHumid = smoothEdge(h, kSwampHumidThreshold, kSwampHumidTransWidth);
  const float swampTemp = smoothEdge(t, kSwampTempThreshold, kSwampTempTransWidth);
  const float forestHumid = smoothEdge(h, kForestHumidThreshold, kForestTransWidth);
  const float oceanTemp = smoothEdge(t, kOceanTempThreshold, kOceanTransWidth);
  const float oceanHumid = smoothEdge(h, kOceanHumidThreshold, kOceanTransWidth);

  result.weights.mountains = mountainWeight;
  result.weights.desert = desertTemp * (1.0f - desertHumid);
  result.weights.swamp = swampHumid * swampTemp;
  // Forest suppressed where mountains or swamp would take priority (mirrors pick() ordering).
  result.weights.forest = forestHumid * (1.0f - result.weights.mountains) * (1.0f - result.weights.swamp);
  // Ocean: cool-ish and dry-ish, suppressed where mountains dominate.
  result.weights.ocean = (1.0f - oceanTemp) * (1.0f - oceanHumid) * (1.0f - result.weights.mountains);
  // Plains: remainder.
  result.weights.plains = 1.0f - result.weights.mountains - result.weights.desert -
                          result.weights.swamp - result.weights.forest - result.weights.ocean;
  if (result.weights.plains < 0.0f) result.weights.plains = 0.0f;

  float total = result.weights.mountains + result.weights.desert + result.weights.swamp +
                result.weights.forest + result.weights.ocean + result.weights.plains;
  if (total > 0.0f) {
    result.weights.mountains /= total;
    result.weights.desert    /= total;
    result.weights.swamp     /= total;
    result.weights.forest    /= total;
    result.weights.ocean     /= total;
    result.weights.plains    /= total;
  }

  result.mountainWeight = result.weights.mountains;

  result.dominantBiome = &PlainsBiome::instance();
  if (t > kDesertTempThreshold && h < kDesertHumidThreshold) {
    result.dominantBiome = &DesertBiome::instance();
  } else if (h > kSwampHumidThreshold && t > kSwampTempThreshold) {
    result.dominantBiome = &SwampBiome::instance();
  } else if (t < kMountainTempThreshold) {
    result.dominantBiome = &MountainsBiome::instance();
  } else if (h > kForestHumidThreshold) {
    result.dominantBiome = &ForestBiome::instance();
  } else if (t < kOceanTempThreshold && h < kOceanHumidThreshold) {
    result.dominantBiome = &OceanBiome::instance();
  }

  result.blendedHeightBias =
    result.weights.mountains * MountainsBiome::instance().heightBias() +
    result.weights.desert    * DesertBiome::instance().heightBias() +
    result.weights.swamp     * SwampBiome::instance().heightBias() +
    result.weights.forest    * ForestBiome::instance().heightBias() +
    result.weights.ocean     * OceanBiome::instance().heightBias() +
    result.weights.plains    * PlainsBiome::instance().heightBias();

  return result;
}

} // namespace

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
  return *evaluateClimate({temperature, humidity}).dominantBiome;
}

const Biome& BiomeFactory::pick(const ClimateSample& c) {
  return *evaluateClimate(c).dominantBiome;
}

auto BiomeFactory::evaluate(const ClimateSample& c) -> BiomeEvaluation {
  return evaluateClimate(c);
}

float BiomeFactory::mountainWeight(const ClimateSample& c) {
  return evaluateClimate(c).mountainWeight;
}

auto BiomeFactory::computeWeights(const ClimateSample& c) -> BiomeWeights {
  return evaluateClimate(c).weights;
}

float BiomeFactory::blendedHeightBias(const ClimateSample& c) {
  return evaluateClimate(c).blendedHeightBias;
}

} // namespace voxel::biome
