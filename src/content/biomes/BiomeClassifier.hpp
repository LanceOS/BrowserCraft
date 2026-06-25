#pragma once

#include "BiomeData.hpp"

namespace voxel::biome {

/// Pure-function biome classification — no noise dependency.
/// All methods operate on ClimateSample or raw temperature/humidity values.
/// This separation allows the classification logic to be tested with
/// synthetic climate values and reused with different noise backends.
class BiomeClassifier {
public:
  /// Hermite smoothstep: maps [threshold - width/2, threshold + width/2]
  /// to [0, 1] with smooth ease-in-out. Values outside the transition
  /// range clamp to 0 or 1.
  static float smoothEdge(float value, float threshold, float width);

  /// Pick a biome from raw temperature/humidity in [0, 1].
  [[nodiscard]] static auto pick(float temperature, float humidity) -> const BiomeSurfaceRule&;

  /// Convenience: pick from a ClimateSample.
  [[nodiscard]] static auto sampleBiome(const ClimateSample& c) -> const BiomeSurfaceRule&;

  /// Smooth [0, 1] mountain weight. High when temperature is low.
  [[nodiscard]] static float mountainWeight(const ClimateSample& c);

  /// Blended height bias using weighted interpolation across all biomes.
  /// Avoids hard walls at biome boundaries.
  [[nodiscard]] static float blendedHeightBias(const ClimateSample& c);
};

} // namespace voxel::biome
