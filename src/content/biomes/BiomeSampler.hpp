#pragma once

#include "BiomeData.hpp"
#include "world/generation/SimplexNoise.hpp"
#include <array>

namespace voxel::biome {

/// Samples temperature and humidity noise, then classifies into biomes.
/// Owns the noise instances but delegates classification to pure functions
/// (pick, blendedHeightBias, mountainWeight) so they can be tested with
/// synthetic values or refactored into a separate classifier.
class BiomeSampler {
public:
  explicit BiomeSampler(uint32_t seed);

  /// Sample temperature noise at world coordinates. Returns [0, 1].
  [[nodiscard]] auto sampleTemperature(float worldX, float worldZ) const -> float;

  /// Sample humidity noise at world coordinates. Returns [0, 1].
  [[nodiscard]] auto sampleHumidity(float worldX, float worldZ) const -> float;

  /// Sample both temperature and humidity in a single call (avoids
  /// duplicate noise evaluations when multiple consumers need climate data
  /// for the same coordinate).
  [[nodiscard]] auto sampleClimate(float worldX, float worldZ) const -> ClimateSample;

  /// Pick the biome surface rule at world coordinates.
  [[nodiscard]] auto sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule&;

  /// Pick the biome surface rule from a pre-sampled climate value.
  /// Avoids redundant noise evaluation when climate is already known.
  [[nodiscard]] static auto sampleBiome(const ClimateSample& climate) -> const BiomeSurfaceRule&;

  /// Pick biome from temperature/humidity (pure function, no noise dependency).
  [[nodiscard]] static auto pick(float temperature, float humidity) -> const BiomeSurfaceRule&;

  /// Smooth [0,1] mountain weight derived from temperature.
  /// Enables the pipeline to apply mountain amplification without hard
  /// discontinuities at biome boundaries.
  [[nodiscard]] auto mountainWeight(float worldX, float worldZ) const -> float;

  /// Mountain weight from a pre-sampled climate value.
  [[nodiscard]] static auto mountainWeight(const ClimateSample& climate) -> float;

  /// Blended height bias at world coordinates.
  /// Uses temperature/humidity-weighted interpolation across all biomes
  /// to avoid hard walls at biome boundaries.
  [[nodiscard]] auto blendedHeightBias(float worldX, float worldZ) const -> float;

  /// Blended height bias from a pre-sampled climate value.
  [[nodiscard]] static auto blendedHeightBias(const ClimateSample& climate) -> float;

private:
  SimplexNoise m_tempNoise;
  SimplexNoise m_humidNoise;
};

} // namespace voxel::biome
