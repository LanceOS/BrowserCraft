#pragma once

#include "SimplexNoise.hpp"

namespace terrain {

struct WorldGenerationConfig;

class MountainGenerator {
public:
  explicit MountainGenerator(uint32_t seed);

  [[nodiscard]] auto sample(float worldX, float worldZ, float mountainWeight, const WorldGenerationConfig& cfg) const -> float;

private:
  SimplexNoise m_noise;
};

} // namespace terrain
