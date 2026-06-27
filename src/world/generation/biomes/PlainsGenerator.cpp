#include "PlainsGenerator.hpp"
#include "../TerrainSampling.hpp"

namespace terrain {

PlainsGenerator::PlainsGenerator(uint32_t seed)
  : m_continentalNoise(seed ^ 0x1a2b3cu),
    m_detailNoise(seed ^ 0x4d5e6fu)
{}

auto PlainsGenerator::sample(float worldX, float worldZ, const WorldGenerationConfig& cfg) const -> float {
  float continental = m_continentalNoise.fractalNoise2D(
      worldX * cfg.continentalScale,
      worldZ * cfg.continentalScale,
      3,
      2.0f,
      0.5f);

  float detail = m_detailNoise.fractalNoise2D(
      worldX * cfg.detailScale,
      worldZ * cfg.detailScale,
      2,
      2.0f,
      0.5f);

  return continental * cfg.continentalAmplitude + detail * cfg.detailAmplitude;
}

} // namespace terrain
