#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "SimplexNoise.hpp"
#include "content/biomes/BiomeFactory.hpp"
#include "content/biomes/BiomeSampler.hpp"
#include "world/terrain/TerrainMaterial.hpp"

#include <glm/glm.hpp>

// @deprecated Legacy terrain-world code retained during the render-only migration to triangle meshes.
namespace terrain {

/// Configuration for world generation noise layering.
/// Terrain height is computed as:
///   surfaceHeight = baseHeight
///                + continental(worldX, worldZ) * continentalAmplitude
///                + detail(worldX, worldZ) * detailAmplitude
///                + blendedHeightBias(worldX, worldZ)
///                + mountainAmplification(worldX, worldZ)
struct WorldGenerationConfig {
  /// Base sea-level reference.
  float baseHeight = 64.0f;

  /// Continental (large-scale) noise — controls basic land/sea shape.
  float continentalScale = 0.008f;
  float continentalAmplitude = 40.0f;

  /// Regional / detail noise — adds hills, valleys, small bumps.
  /// Higher scale and amplitude than continental to break up flat terraces
  /// caused by int32_t truncation of slowly-varying noise.
  float detailScale = 0.05f;
  float detailAmplitude = 14.0f;

  /// Mountain amplification — extra high-frequency noise in cold regions.
  float mountainScale = 0.02f;
  float mountainAmplitude = 28.0f;

  /// Sea level (for filling water).
  int32_t seaLevel = 64;

  /// 3D density noise — creates underground cavities / overhangs.
  float densityNoiseScale = 0.04f;
  /// How quickly the density threshold increases with depth.
  /// Higher values = fewer surface cavities, more underground only.
  float densityDepthScale = 0.12f;
};

using MaterialId = terrain::MaterialId;
using TerrainMaterial = terrain::TerrainMaterial;

/// Continuous terrain state for a single world-space x/z column.
/// `surfaceHeight` is the unclamped float surface used by the smooth mesher.
/// `surfaceY` mirrors the legacy terrain column logic.
/// `biome` points at the dominant biome for the column.
/// `biomeId`, `temperature`, and `humidity` carry the climate context into the
/// terrain material system.
/// `surfaceDepth` mirrors the biome surface layering used by the legacy block
/// generator so the smooth renderer can keep a similar material profile.
/// `noWater` matches the legacy rule that deserts and oceans skip water fill.
struct TerrainSample {
  float surfaceHeight = 0.0f;
  int32_t surfaceY = 0;
  const biome::Biome* biome = nullptr;
  biome::BiomeId biomeId = biome::BiomeId::Plains;
  float temperature = 0.0f;
  float humidity = 0.0f;
  int32_t surfaceDepth = 4;
  bool noWater = false;
};

/// Reusable terrain sampler for the continuous surface field.
/// It captures the current noise stack and biome selection rules so the
/// smooth terrain mesher can sample the same world-generation logic directly.
class TerrainSampler {
public:
  /// Construct with a seed — creates a default BiomeSampler internally.
  explicit TerrainSampler(uint32_t seed, const WorldGenerationConfig& config = {})
    : m_ownedSampler(std::make_unique<biome::BiomeSampler>(seed)),
      m_climateSource(m_ownedSampler.get()),
      m_continentalNoise(seed ^ 0x1a2b3cu),
      m_detailNoise(seed ^ 0x4d5e6fu),
      m_densityNoise(seed),
      m_config(config)
  {}

  /// Construct with an external climate source (non-owning reference).
  explicit TerrainSampler(biome::IClimateSource& climateSource,
                          uint32_t seed,
                          const WorldGenerationConfig& config = {})
    : m_ownedSampler(nullptr),
      m_climateSource(&climateSource),
      m_continentalNoise(seed ^ 0x1a2b3cu),
      m_detailNoise(seed ^ 0x4d5e6fu),
      m_densityNoise(seed),
      m_config(config)
  {}

  /// Sample the continuous terrain state for a world-space x/z column.
  [[nodiscard]] auto sampleTerrain(float worldX, float worldZ) const -> TerrainSample;

  /// Sample signed density. Negative = solid, positive = air, zero = surface.
  [[nodiscard]] auto sampleDensity(float worldX, float worldY, float worldZ) const -> float;

  /// Sample the terrain material for a world coordinate.
  /// Returns blended terrain material hints derived from slope, depth, and
  /// biome context.
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ) const -> TerrainMaterial;

  /// Sample the terrain material for a world coordinate using a surface normal
  /// to estimate slope. This is the preferred path for smooth terrain meshes.
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ,
                                    const glm::vec3& normal) const -> TerrainMaterial;

  /// Access the sampler configuration.
  [[nodiscard]] auto config() const -> const WorldGenerationConfig& { return m_config; }

private:
  [[nodiscard]] static auto fractalNoise2D(const SimplexNoise& noise,
                                           float x, float z,
                                           int octaves,
                                           float lacunarity,
                                           float persistence) -> float;

  std::unique_ptr<biome::BiomeSampler> m_ownedSampler;
  biome::IClimateSource* m_climateSource;
  SimplexNoise m_continentalNoise;
  SimplexNoise m_detailNoise;
  SimplexNoise m_densityNoise;
  WorldGenerationConfig m_config;
};

inline auto TerrainSampler::fractalNoise2D(const SimplexNoise& noise,
                                           float x, float z,
                                           int octaves,
                                           float lacunarity,
                                           float persistence) -> float {
  float value = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float maxAmplitude = 0.0f;

  for (int i = 0; i < octaves; ++i) {
    value += amplitude * noise.noise3D(x * frequency, 0.0f, z * frequency);
    maxAmplitude += amplitude;
    frequency *= lacunarity;
    amplitude *= persistence;
  }

  // Normalise to roughly [-1, 1]
  return maxAmplitude > 0.0f ? value / maxAmplitude : 0.0f;
}

inline auto TerrainSampler::sampleTerrain(float worldX, float worldZ) const -> TerrainSample {
  const auto& cfg = m_config;

  float continental = fractalNoise2D(
      m_continentalNoise,
      worldX * cfg.continentalScale,
      worldZ * cfg.continentalScale,
      3,
      2.0f,
      0.5f);

  float detail = fractalNoise2D(
      m_detailNoise,
      worldX * cfg.detailScale,
      worldZ * cfg.detailScale,
      2,
      2.0f,
      0.5f);

  auto climate = m_climateSource->sampleClimate(worldX, worldZ);
  auto climateEval = biome::BiomeFactory::evaluate(climate);
  const auto* activeBiome = climateEval.dominantBiome;

  float mountainExtra = climateEval.mountainWeight * fractalNoise2D(
      m_continentalNoise,
      worldX * cfg.mountainScale,
      worldZ * cfg.mountainScale,
      3,
      2.5f,
      0.6f) * cfg.mountainAmplitude;
  if (mountainExtra < 0.0f) mountainExtra *= -0.6f;

  TerrainSample sample;
  sample.surfaceHeight =
      cfg.baseHeight
      + continental * cfg.continentalAmplitude
      + detail * cfg.detailAmplitude
      + climateEval.blendedHeightBias
      + mountainExtra;
  sample.surfaceY = static_cast<int32_t>(sample.surfaceHeight);
  sample.biome = activeBiome;
  sample.biomeId = activeBiome ? activeBiome->id() : biome::BiomeId::Plains;
  sample.temperature = climate.temperature;
  sample.humidity = climate.humidity;
  sample.surfaceDepth = activeBiome ? activeBiome->surfaceDepth() : 4;
  if (activeBiome) {
    sample.noWater = (activeBiome->id() == biome::BiomeId::Desert ||
                      activeBiome->id() == biome::BiomeId::Ocean);
  }

  return sample;
}

inline auto TerrainSampler::sampleDensity(float worldX, float worldY, float worldZ) const -> float {
  const auto terrain = sampleTerrain(worldX, worldZ);
  float density = worldY - terrain.surfaceHeight;

  const auto& cfg = m_config;
  if (cfg.densityNoiseScale > 0.0f) {
    const float depthBelowSurface = terrain.surfaceHeight - worldY;
    if (depthBelowSurface > 5.0f) {
      const float caveNoise = m_densityNoise.noise3D(
          worldX * cfg.densityNoiseScale,
          worldY * cfg.densityNoiseScale,
          worldZ * cfg.densityNoiseScale);

      // Positive depthFactor reinforces the solid mass deeper underground,
      // while negative noise carves out air pockets and overhangs.
      const float depthFactor = depthBelowSurface * cfg.densityDepthScale;
      density -= (caveNoise + depthFactor) * 4.0f;
    }
  }

  return density;
}

inline auto TerrainSampler::sampleMaterial(float worldX, float worldY, float worldZ) const -> TerrainMaterial {
  return sampleMaterial(worldX, worldY, worldZ, glm::vec3(0.0f, 1.0f, 0.0f));
}

inline auto TerrainSampler::sampleMaterial(float worldX, float worldY, float worldZ,
                                           const glm::vec3& normal) const -> TerrainMaterial {
  const auto terrain = sampleTerrain(worldX, worldZ);
  terrain::TerrainMaterialContext ctx{};
  ctx.surfaceHeight = terrain.surfaceHeight;
  ctx.seaLevel = static_cast<float>(m_config.seaLevel);
  ctx.depthBelowSurface = terrain.surfaceHeight - worldY;
  ctx.slope = std::clamp(1.0f - std::abs(normal.y), 0.0f, 1.0f);
  ctx.temperature = terrain.temperature;
  ctx.humidity = terrain.humidity;
  ctx.biomeId = terrain.biomeId;
  ctx.surfaceDepth = terrain.surfaceDepth;
  ctx.noWater = terrain.noWater;
  return terrain::resolveTerrainMaterial(ctx);
}

} // namespace terrain
