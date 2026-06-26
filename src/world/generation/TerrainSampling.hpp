#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "SimplexNoise.hpp"
#include "content/biomes/BiomeFactory.hpp"
#include "content/biomes/BiomeSampler.hpp"
#include "world/BlockIds.hpp"

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

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

/// Terrain material categories used by the continuous sampler.
/// These intentionally model the terrain surface materials only. Water,
/// air, and other special voxel states remain on the legacy block path.
enum class MaterialId : uint8_t {
  Grass = 0,
  Dirt  = 1,
  Stone = 2,
  Sand  = 3,
};

using TerrainMaterial = MaterialId;

/// Continuous terrain state for a single world-space x/z column.
/// `surfaceHeight` is the unclamped float surface used by the smooth mesher.
/// `surfaceY` mirrors the legacy voxel column logic.
/// `biome` points at the dominant biome for the column.
/// `noWater` matches the legacy rule that deserts and oceans skip water fill.
struct TerrainSample {
  float surfaceHeight = 0.0f;
  int32_t surfaceY = 0;
  const biome::Biome* biome = nullptr;
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
  /// Returns the same top/filler/stone/sand layering used by voxel terrain.
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ) const -> MaterialId;

  /// Access the sampler configuration.
  [[nodiscard]] auto config() const -> const WorldGenerationConfig& { return m_config; }

private:
  [[nodiscard]] static auto fractalNoise2D(const SimplexNoise& noise,
                                           float x, float z,
                                           int octaves,
                                           float lacunarity,
                                           float persistence) -> float;

  [[nodiscard]] static constexpr auto materialFromBlock(uint8_t blockId) -> MaterialId {
    switch (blockId) {
      case BlockId::GRASS: return MaterialId::Grass;
      case BlockId::DIRT:  return MaterialId::Dirt;
      case BlockId::SAND:  return MaterialId::Sand;
      case BlockId::STONE:
      case BlockId::BEDROCK:
      default:
        return MaterialId::Stone;
    }
  }

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

inline auto TerrainSampler::sampleMaterial(float worldX, float worldY, float worldZ) const -> MaterialId {
  const auto terrain = sampleTerrain(worldX, worldZ);
  if (!terrain.biome) {
    return MaterialId::Stone;
  }

  const int32_t blockY = static_cast<int32_t>(std::floor(worldY));
  if (blockY <= 0) {
    return MaterialId::Stone;
  }

  if (blockY >= terrain.surfaceY) {
    return materialFromBlock(terrain.biome->topBlock());
  }
  if (blockY > terrain.surfaceY - terrain.biome->surfaceDepth()) {
    return materialFromBlock(terrain.biome->fillerBlock());
  }
  return MaterialId::Stone;
}

} // namespace voxel
