#pragma once

#include "BiomeData.hpp"

namespace voxel::biome {

/// Result of classifying a climate sample.
/// Kept separate from BiomeFactory so climate rules can evolve independently
/// from concrete biome registration / lookup.
struct BiomeClassification {
  BiomeId dominantBiomeId = BiomeId::Plains;
  BiomeWeights weights{};
  float mountainWeight = 0.0f;
  float blendedHeightBias = 0.0f;
};

/// Pure climate classifier. Converts temperature / humidity samples into
/// biome IDs and smooth blending values, but does not know about concrete
/// biome singleton instances.
class BiomeClassifier {
public:
  [[nodiscard]] static auto pick(const ClimateSample& c) -> BiomeId;
  [[nodiscard]] static auto evaluate(const ClimateSample& c) -> BiomeClassification;
  [[nodiscard]] static float mountainWeight(const ClimateSample& c);
  [[nodiscard]] static auto computeWeights(const ClimateSample& c) -> BiomeWeights;
  [[nodiscard]] static float blendedHeightBias(const ClimateSample& c);
};

} // namespace voxel::biome
