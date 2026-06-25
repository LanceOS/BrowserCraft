#pragma once

#include "BiomeData.hpp"

namespace voxel::biome {

/// Pure-function biome classification — no noise dependency.
/// All methods operate on ClimateSample or raw temperature/humidity values.
/// This separation allows the classification logic to be tested with
/// synthetic climate values and reused with different noise backends.
class BiomeClassifier {
public:
  /// Pick a biome from raw temperature/humidity in [0, 1].
  [[nodiscard]] static auto pick(float temperature, float humidity) -> const BiomeSurfaceRule&;

  /// Convenience: pick from a ClimateSample.
  [[nodiscard]] static auto sampleBiome(const ClimateSample& c) -> const BiomeSurfaceRule&;

  /// Smooth [0, 1] mountain weight. High when temperature is low.
  [[nodiscard]] static float mountainWeight(const ClimateSample& c);

  /// Compute per-biome weights for a given climate sample.
  /// The weights reflect the smooth transition between biomes and sum to
  /// approximately 1 (after internal normalisation). Useful for other systems
  /// (flora, mob spawning) that need to know which biomes are present.
  [[nodiscard]] static auto computeWeights(const ClimateSample& c) -> BiomeWeights;

  /// Blended height bias using weighted interpolation across all biomes.
  /// Avoids hard walls at biome boundaries.
  /// Implemented in terms of computeWeights().
  [[nodiscard]] static float blendedHeightBias(const ClimateSample& c);
};

} // namespace voxel::biome
