#pragma once

#include "BiomeData.hpp"
#include "world/generation/SimplexNoise.hpp"
#include <array>

namespace voxel::biome {

/// Samples temperature and humidity to pick a biome.
class BiomeSampler {
public:
  explicit BiomeSampler(uint32_t seed);

  /// Get the height-map noise value at world coordinates.
  [[nodiscard]] auto noise2D(float worldX, float worldZ) const -> float;

  /// Pick the biome surface rule at world coordinates.
  [[nodiscard]] auto sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule&;

  /// Pick biome from temperature/humidity.
  [[nodiscard]] static auto pick(float temperature, float humidity) -> const BiomeSurfaceRule&;

private:
  SimplexNoise m_tempNoise;
  SimplexNoise m_humidNoise;
};

} // namespace voxel::biome
