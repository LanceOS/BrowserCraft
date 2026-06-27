#pragma once

#include "../SimplexNoise.hpp"

namespace terrain {

struct WorldGenerationConfig;

class LakeGenerator {
public:
  explicit LakeGenerator(uint32_t seed);

  [[nodiscard]] auto sample(float worldX, float worldZ, const WorldGenerationConfig& cfg) const -> float;

private:
  SimplexNoise m_noise;
};

} // namespace terrain
