#include "BiomeFactory.hpp"
#include "BiomeClassifier.hpp"

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
  return forId(BiomeClassifier::pick(ClimateSample{temperature, humidity}));
}

const Biome& BiomeFactory::pick(const ClimateSample& c) {
  return forId(BiomeClassifier::pick(c));
}

auto BiomeFactory::evaluate(const ClimateSample& c) -> BiomeEvaluation {
  const auto classification = BiomeClassifier::evaluate(c);
  BiomeEvaluation result;
  result.dominantBiome = &forId(classification.dominantBiomeId);
  result.weights = classification.weights;
  result.mountainWeight = classification.mountainWeight;
  result.blendedHeightBias = classification.blendedHeightBias;
  return result;
}

float BiomeFactory::mountainWeight(const ClimateSample& c) {
  return BiomeClassifier::mountainWeight(c);
}

auto BiomeFactory::computeWeights(const ClimateSample& c) -> BiomeWeights {
  return BiomeClassifier::computeWeights(c);
}

float BiomeFactory::blendedHeightBias(const ClimateSample& c) {
  return BiomeClassifier::blendedHeightBias(c);
}

} // namespace voxel::biome
