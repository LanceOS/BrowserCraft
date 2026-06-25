#pragma once

#include "BiomeData.hpp"
#include "world/generation/SimplexNoise.hpp"
#include <array>

namespace voxel::biome {

/// Samples temperature, humidity, and height to pick a biome.
class BiomeSampler {
public:
  explicit BiomeSampler(uint32_t seed);

  /// Get the continental height-map noise value at world coordinates.
  /// Uses a dedicated height noise (not temperature or humidity).
  [[nodiscard]] auto noise2D(float worldX, float worldZ) const -> float;

  /// Pick the biome surface rule at world coordinates.
  [[nodiscard]] auto sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule&;

  /// Pick biome from temperature/humidity.
  [[nodiscard]] static auto pick(float temperature, float humidity) -> const BiomeSurfaceRule&;

  /// Get a blended height bias at world coordinates.
  /// Uses noise-weighted interpolation to avoid hard biome walls.
  [[nodiscard]] auto blendedHeightBias(float worldX, float worldZ) const -> float;

private:
  SimplexNoise m_tempNoise;
  SimplexNoise m_humidNoise;
  SimplexNoise m_heightNoise;
};

} // namespace voxel::biome
