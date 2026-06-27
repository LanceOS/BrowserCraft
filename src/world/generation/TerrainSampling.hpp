#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "SimplexNoise.hpp"
#include "biomes/MountainGenerator.hpp"
#include "biomes/PlainsGenerator.hpp"
#include "biomes/RiverGenerator.hpp"
#include "biomes/LakeGenerator.hpp"
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
  float continentalScale = 0.0005f;
  float continentalAmplitude = 12.0f;

  /// Regional / detail noise — adds hills, valleys, small bumps.
  float detailScale = 0.002f;
  float detailAmplitude = 4.0f;

  /// Mountain amplification — extra high-frequency noise in cold regions.
  float mountainScale = 0.001f;
  float mountainAmplitude = 10.0f;

  /// River carving (ridged noise)
  float riverScale = 0.001f;
  float riverDepth = 8.0f;

  /// Lake basins
  float lakeScale = 0.003f;
  float lakeDepth = 12.0f;

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
      m_densityNoise(seed),
      m_plainsGenerator(seed),
      m_mountainGenerator(seed),
      m_riverGenerator(seed),
      m_lakeGenerator(seed),
      m_config(config)
  {}

  /// Construct with an external climate source (non-owning reference).
  explicit TerrainSampler(biome::IClimateSource& climateSource,
                          uint32_t seed,
                          const WorldGenerationConfig& config = {})
    : m_ownedSampler(nullptr),
      m_climateSource(&climateSource),
      m_densityNoise(seed),
      m_plainsGenerator(seed),
      m_mountainGenerator(seed),
      m_riverGenerator(seed),
      m_lakeGenerator(seed),
      m_config(config)
  {}

  /// Sample the continuous terrain state for a world-space x/z column.
  [[nodiscard]] auto sampleTerrain(float worldX, float worldZ) const -> TerrainSample;

  /// Sample signed density. Negative = solid, positive = air, zero = surface.
  [[nodiscard]] auto sampleDensity(float worldX, float worldY, float worldZ) const -> float;
  [[nodiscard]] auto sampleDensity(float worldX, float worldY, float worldZ, const TerrainSample& terrain) const -> float;

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
  std::unique_ptr<biome::BiomeSampler> m_ownedSampler;
  biome::IClimateSource* m_climateSource;
  SimplexNoise m_densityNoise;
  PlainsGenerator m_plainsGenerator;
  MountainGenerator m_mountainGenerator;
  RiverGenerator m_riverGenerator;
  LakeGenerator m_lakeGenerator;
  WorldGenerationConfig m_config;
};

// removed fractalNoise2D

inline auto TerrainSampler::sampleTerrain(float worldX, float worldZ) const -> TerrainSample {
  const auto& cfg = m_config;

  float baseTerrainHeight = m_plainsGenerator.sample(worldX, worldZ, cfg);

  auto climate = m_climateSource->sampleClimate(worldX, worldZ);
  auto climateEval = biome::BiomeFactory::evaluate(climate);
  const auto* activeBiome = climateEval.dominantBiome;

  float mountainExtra = m_mountainGenerator.sample(worldX, worldZ, climateEval.mountainWeight, cfg);
  
  float riverCarve = m_riverGenerator.sample(worldX, worldZ, cfg);
  float lakeCarve = m_lakeGenerator.sample(worldX, worldZ, cfg);

  TerrainSample sample;
  sample.surfaceHeight =
      cfg.baseHeight
      + baseTerrainHeight
      + climateEval.blendedHeightBias
      + mountainExtra
      + riverCarve
      + lakeCarve;
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

inline auto TerrainSampler::sampleDensity(float worldX, float worldY, float worldZ, const TerrainSample& terrain) const -> float {
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

inline auto TerrainSampler::sampleDensity(float worldX, float worldY, float worldZ) const -> float {
  return sampleDensity(worldX, worldY, worldZ, sampleTerrain(worldX, worldZ));
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
