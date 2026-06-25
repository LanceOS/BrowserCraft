#pragma once

#include "BiomeData.hpp"
#include "BiomeClassifier.hpp"
#include "IClimateSource.hpp"
#include "world/generation/SimplexNoise.hpp"
#include <array>

namespace voxel::biome {

/// Samples temperature and humidity noise, then classifies into biomes.
/// Owns the noise instances but delegates classification logic to the
/// stateless BiomeClassifier (which can be tested with synthetic values).
/// Implements IClimateSource so it can be passed to WorldGenPipeline
/// or replaced with a different climate strategy.
class BiomeSampler : public IClimateSource {
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

  /// Pick the biome surface rule at world coordinates (convenience).
  [[nodiscard]] auto sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule&;

  /// Smooth [0,1] mountain weight at world coordinates (convenience).
  [[nodiscard]] auto mountainWeight(float worldX, float worldZ) const -> float;

  /// Blended height bias at world coordinates (convenience).
  [[nodiscard]] auto blendedHeightBias(float worldX, float worldZ) const -> float;

private:
  SimplexNoise m_tempNoise;
  SimplexNoise m_humidNoise;
};

} // namespace voxel::biome
