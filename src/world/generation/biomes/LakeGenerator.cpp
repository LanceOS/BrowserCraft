#include "LakeGenerator.hpp"
#include "../TerrainSampling.hpp"

#include <cmath>

namespace terrain {

LakeGenerator::LakeGenerator(uint32_t seed)
  : m_noise(seed ^ 0x667788)
{}

auto LakeGenerator::sample(float worldX, float worldZ, const WorldGenerationConfig& cfg) const -> float {
  float n = m_noise.fractalNoise2D(
      worldX * cfg.lakeScale,
      worldZ * cfg.lakeScale,
      2,     // octaves
      2.0f,  // lacunarity
      0.5f   // persistence
  );

  // 'n' is roughly [-1, 1]. Carve lakes where noise is particularly low.
  const float lakeThreshold = -0.3f;
  if (n < lakeThreshold) {
    float depth = (lakeThreshold - n) / (1.0f + lakeThreshold); // Normalize depth 0.0 to 1.0
    
    // Smooth the edges of the lake basin
    depth = std::pow(depth, 2.0f);
    
    return -depth * cfg.lakeDepth;
  }
  
  return 0.0f;
}

} // namespace terrain
