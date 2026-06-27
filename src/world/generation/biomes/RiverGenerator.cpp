#include "RiverGenerator.hpp"
#include "../TerrainSampling.hpp"

#include <cmath>

namespace terrain {

RiverGenerator::RiverGenerator(uint32_t seed)
  : m_noise(seed ^ 0x334455)
{}

auto RiverGenerator::sample(float worldX, float worldZ, const WorldGenerationConfig& cfg) const -> float {
  // Use a fractal ridge noise to carve a network of rivers
  float n = m_noise.fractalNoise2D(
      worldX * cfg.riverScale,
      worldZ * cfg.riverScale,
      2,     // octaves
      2.0f,  // lacunarity
      0.5f   // persistence
  );

  // 'n' is roughly [-1, 1]. We want the river at the zero-crossings.
  float distToRiver = std::abs(n);

  // Invert it so 1.0 is the river center
  float carve = 1.0f - distToRiver;
  if (carve < 0.0f) carve = 0.0f;

  // Sharpen the river banks
  carve = std::pow(carve, 4.0f);

  return -carve * cfg.riverDepth;
}

} // namespace terrain
