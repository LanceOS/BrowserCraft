#include "BiomeSampler.hpp"
#include <algorithm>
#include <cmath>

namespace voxel::biome {

BiomeSampler::BiomeSampler(uint32_t seed)
  : m_tempNoise(seed ^ 0xa10beu),
    m_humidNoise(seed ^ 0xb1d07u)
{}

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
  // Transition centers are aligned with pick() thresholds so that the
  // surface rule selection and height bias remain consistent.
  auto smoothEdge = [](float value, float threshold, float width) -> float {
    float edge = (value - threshold + width * 0.5f) / width;
    edge = std::clamp(edge, 0.0f, 1.0f);
    // Hermite smoothstep
    return edge * edge * (3.0f - 2.0f * edge);
  };

  // Mountains: pick() uses t < 0.28
  float wMountains = 1.0f - smoothEdge(t, 0.28f, 0.08f);
  // Desert: pick() uses t > 0.65 && h < 0.35
  float wDesert = smoothEdge(t, 0.65f, 0.08f) * (1.0f - smoothEdge(h, 0.35f, 0.08f));
  // Swamp: pick() uses h > 0.72 && t > 0.35
  float wSwamp = smoothEdge(h, 0.72f, 0.10f) * smoothEdge(t, 0.35f, 0.08f);
  // Forest: pick() uses h > 0.55; suppress where mountains or swamp would take priority
  float wForest = smoothEdge(h, 0.55f, 0.08f) * (1.0f - wMountains) * (1.0f - wSwamp);
  // Plains: remainder — naturally falls to near zero when another biome dominates
  float wPlains = std::max(0.0f, 1.0f - wMountains - wDesert - wSwamp - wForest);

  // Normalise weights so they sum to 1.
  float total = wMountains + wDesert + wSwamp + wForest + wPlains;
  // Guard against division by zero if all weights are somehow zero (shouldn't happen).
  if (total <= 0.0f) return PlainsBiome.heightBias;

  // Weighted sum of biome height biases.
  return
    (wMountains / total) * MountainsBiome.heightBias +
    (wDesert    / total) * DesertBiome.heightBias +
    (wSwamp     / total) * SwampBiome.heightBias +
    (wForest    / total) * ForestBiome.heightBias +
    (wPlains    / total) * PlainsBiome.heightBias;
}

auto BiomeSampler::mountainWeight(float worldX, float worldZ) const -> float {
  // Evaluate temperature and derive mountain influence using the same
  // smooth transition as blendedHeightBias — centered at 0.28, width 0.08.
  float t = (m_tempNoise.noise3D(worldX * 0.008f, 0.0f, worldZ * 0.008f) + 1.0f) * 0.5f;
  float edge = (t - 0.28f + 0.04f) / 0.08f;
  edge = std::clamp(edge, 0.0f, 1.0f);
  // Hermite smoothstep, inverted: high when cold (t low)
  float w = 1.0f - (edge * edge * (3.0f - 2.0f * edge));
  return std::max(0.0f, w);
}

auto BiomeSampler::pick(float temperature, float humidity) -> const BiomeSurfaceRule& {
  if (temperature > 0.65f && humidity < 0.35f) return DesertBiome;
  if (humidity > 0.72f && temperature > 0.35f)  return SwampBiome;
  if (temperature < 0.28f)                        return MountainsBiome;
  if (humidity > 0.55f)                           return ForestBiome;
  return PlainsBiome;
}

} // namespace voxel::biome
