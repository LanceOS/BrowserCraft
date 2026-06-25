#include "BiomeClassifier.hpp"

namespace voxel::biome {

namespace {

auto evaluateClimate(const ClimateSample& c) -> BiomeClassification {
  const float t = c.temperature;
  const float h = c.humidity;

  BiomeClassification result;

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
  // Forest suppressed where mountains or swamp would take priority.
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

  result.dominantBiomeId = BiomeId::Plains;
  if (t > kDesertTempThreshold && h < kDesertHumidThreshold) {
    result.dominantBiomeId = BiomeId::Desert;
  } else if (h > kSwampHumidThreshold && t > kSwampTempThreshold) {
    result.dominantBiomeId = BiomeId::Swamp;
  } else if (t < kMountainTempThreshold) {
    result.dominantBiomeId = BiomeId::Mountains;
  } else if (h > kForestHumidThreshold) {
    result.dominantBiomeId = BiomeId::Forest;
  } else if (t < kOceanTempThreshold && h < kOceanHumidThreshold) {
    result.dominantBiomeId = BiomeId::Ocean;
  }

  result.blendedHeightBias =
    result.weights.mountains * kMountainsHeightBias +
    result.weights.desert    * kDesertHeightBias +
    result.weights.swamp     * kSwampHeightBias +
    result.weights.forest    * kForestHeightBias +
    result.weights.ocean     * kOceanHeightBias +
    result.weights.plains    * kPlainsHeightBias;

  return result;
}

} // namespace

auto BiomeClassifier::pick(const ClimateSample& c) -> BiomeId {
  return evaluateClimate(c).dominantBiomeId;
}

auto BiomeClassifier::evaluate(const ClimateSample& c) -> BiomeClassification {
  return evaluateClimate(c);
}

float BiomeClassifier::mountainWeight(const ClimateSample& c) {
  return evaluateClimate(c).mountainWeight;
}

auto BiomeClassifier::computeWeights(const ClimateSample& c) -> BiomeWeights {
  return evaluateClimate(c).weights;
}

float BiomeClassifier::blendedHeightBias(const ClimateSample& c) {
  return evaluateClimate(c).blendedHeightBias;
}

} // namespace voxel::biome
