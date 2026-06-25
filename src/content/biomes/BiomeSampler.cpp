#include "BiomeSampler.hpp"
#include <algorithm>
#include <cmath>

namespace voxel::biome {

BiomeSampler::BiomeSampler(uint32_t seed)
  : m_tempNoise(seed ^ 0xa10beu),
    m_humidNoise(seed ^ 0xb1d07u),
    m_heightNoise(seed ^ 0xc0da7au)   // dedicated continental height noise
{}

auto BiomeSampler::noise2D(float worldX, float worldZ) const -> float {
  // Use dedicated height noise instead of temperature noise.
  // This provides proper large-scale terrain contours independent of biome climate.
  return m_heightNoise.noise3D(worldX, 0.0f, worldZ);
}

auto BiomeSampler::sampleBiome(float worldX, float worldZ) const -> const BiomeSurfaceRule& {
  float temperature = (m_tempNoise.noise3D(worldX * 0.008f, 0.0f, worldZ * 0.008f) + 1.0f) * 0.5f;
  float humidity = (m_humidNoise.noise3D(worldX * 0.008f, 100.0f, worldZ * 0.008f) + 1.0f) * 0.5f;
  return pick(temperature, humidity);
}

auto BiomeSampler::blendedHeightBias(float worldX, float worldZ) const -> float {
  // Sample temperature and humidity in [0, 1].
  float t = (m_tempNoise.noise3D(worldX * 0.008f, 0.0f, worldZ * 0.008f) + 1.0f) * 0.5f;
  float h = (m_humidNoise.noise3D(worldX * 0.008f, 100.0f, worldZ * 0.008f) + 1.0f) * 0.5f;

  // Evaluate weight for each biome using smoothstep-like blending.
  // This replaces the hard cutoffs in pick() with smooth transitions.
  auto smoothEdge = [](float value, float threshold, float width) -> float {
    float edge = (value - threshold + width * 0.5f) / width;
    edge = std::clamp(edge, 0.0f, 1.0f);
    // Hermite smoothstep
    return edge * edge * (3.0f - 2.0f * edge);
  };

  float wMountains = 1.0f - smoothEdge(t, 0.30f, 0.10f);
  float wDesert    = smoothEdge(t, 0.60f, 0.10f) * (1.0f - smoothEdge(h, 0.40f, 0.10f));
  float wSwamp     = smoothEdge(h, 0.65f, 0.12f) * smoothEdge(t, 0.30f, 0.10f);
  float wForest    = smoothEdge(h, 0.50f, 0.10f) * (1.0f - wMountains) * (1.0f - wSwamp);

  // Normalise weights so they sum to 1.
  float total = wMountains + wDesert + wSwamp + wForest + 1.0f; // +1 for plains (default)
  wMountains /= total;
  wDesert    /= total;
  wSwamp     /= total;
  wForest    /= total;
  float wPlains = 1.0f / total;

  // Weighted sum of biome height biases.
  float bias =
    wMountains * MountainsBiome.heightBias +
    wDesert    * DesertBiome.heightBias +
    wSwamp     * SwampBiome.heightBias +
    wForest    * ForestBiome.heightBias +
    wPlains    * PlainsBiome.heightBias;

  return bias;
}

auto BiomeSampler::pick(float temperature, float humidity) -> const BiomeSurfaceRule& {
  if (temperature > 0.65f && humidity < 0.35f) return DesertBiome;
  if (humidity > 0.72f && temperature > 0.35f)  return SwampBiome;
  if (temperature < 0.28f)                        return MountainsBiome;
  if (humidity > 0.55f)                           return ForestBiome;
  return PlainsBiome;
}

} // namespace voxel::biome
