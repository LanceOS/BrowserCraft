#pragma once

#include "BiomeData.hpp"
#include "world/generation/SimplexNoise.hpp"
#include <array>

namespace voxel::biome {

/// Samples temperature, humidity, and height to pick a biome.
class BiomeSampler {
public:
  explicit BiomeSampler(uint32_t seed);

  /// Pick the biome surface rule at world coordinates.
  [[nodiscard]] auto sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule&;

  /// Pick biome from temperature/humidity.
  [[nodiscard]] static auto pick(float temperature, float humidity) -> const BiomeSurfaceRule&;

  /// Return a smooth [0,1] weight indicating how strongly mountains influence
  /// this location. Uses the same temperature-based transition as pick() but
  /// with smoothstep blending so the pipeline can apply mountain amplification
  /// without hard discontinuities at biome boundaries.
  [[nodiscard]] auto mountainWeight(float worldX, float worldZ) const -> float;

  /// Get a blended height bias at world coordinates.
  /// Uses noise-weighted interpolation to avoid hard biome walls.
  [[nodiscard]] auto blendedHeightBias(float worldX, float worldZ) const -> float;

private:
  SimplexNoise m_tempNoise;
  SimplexNoise m_humidNoise;
};

} // namespace voxel::biome
