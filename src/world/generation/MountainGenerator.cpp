#include "MountainGenerator.hpp"
#include "TerrainSampling.hpp"

namespace terrain {

MountainGenerator::MountainGenerator(uint32_t seed)
  : m_noise(seed ^ 0x9f2b8a)
{}

auto MountainGenerator::sample(float worldX, float worldZ, float mountainWeight, const WorldGenerationConfig& cfg) const -> float {
  if (mountainWeight <= 0.0f) return 0.0f;

  float extra = mountainWeight * m_noise.fractalNoise2D(
      worldX * cfg.mountainScale,
      worldZ * cfg.mountainScale,
      3,
      2.5f,
      0.6f) * cfg.mountainAmplitude;

  // Enhance the peaks, reduce the valleys
  if (extra < 0.0f) {
    extra *= -0.6f;
  }

  return extra;
}

} // namespace terrain
