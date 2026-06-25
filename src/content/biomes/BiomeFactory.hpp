#pragma once

#include "BiomeData.hpp"
#include "Biome.hpp"

namespace voxel::biome {

/// Routes climate samples to concrete Biome instances and computes
/// blended weights / height bias across all biomes.
///
/// This is the only place that knows about all concrete biome types.
/// Adding a new biome requires: (1) adding the class in Biome.hpp,
/// (2) registering it in pick() and computeWeights() here.
class BiomeFactory {
public:
  /// Look up a biome by its BiomeId.
  [[nodiscard]] static const Biome& forId(BiomeId id);

  /// Pick a single biome from raw temperature/humidity in [0, 1].
  /// Uses hard thresholds — returns the dominant biome for this climate.
  [[nodiscard]] static const Biome& pick(float temperature, float humidity);

  /// Convenience: pick from a ClimateSample.
  [[nodiscard]] static const Biome& pick(const ClimateSample& c);

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
