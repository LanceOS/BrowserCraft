#pragma once

#include "../SimplexNoise.hpp"

namespace terrain {

struct WorldGenerationConfig;

class PlainsGenerator {
public:
  explicit PlainsGenerator(uint32_t seed);

  [[nodiscard]] auto sample(float worldX, float worldZ, const WorldGenerationConfig& cfg) const -> float;

private:
  SimplexNoise m_continentalNoise;
  SimplexNoise m_detailNoise;
};

} // namespace terrain
