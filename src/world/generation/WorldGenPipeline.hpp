#pragma once

#include <cstdint>
#include "TerrainSampling.hpp"
#include "SimplexNoise.hpp"

namespace terrain {

/// Full world generation pipeline for a single chunk.
/// Terrain sampling is delegated to TerrainSampler so the continuous surface
/// logic can be reused by smooth meshers without changing the legacy terrain
/// meshing path.
class WorldGenPipeline {
public:
  /// Construct with a seed — creates a default BiomeSampler internally.
  explicit WorldGenPipeline(uint32_t seed, const WorldGenerationConfig& config = {});

  /// Construct with an external climate source (non-owning reference).
  /// The seed parameter is used for terrain noise (continental and detail)
  /// independent of the climate source.
  explicit WorldGenPipeline(biome::IClimateSource& climateSource,
                            uint32_t seed,
                            const WorldGenerationConfig& config = {});

  [[nodiscard]] auto sampleTerrain(float worldX, float worldZ) const -> TerrainSample;

  /// Continuous signed-distance-like sampling for smooth meshers.
  [[nodiscard]] auto sampleDensity(float worldX, float worldY, float worldZ) const -> float;
  [[nodiscard]] auto sampleDensity(float worldX, float worldY, float worldZ, const TerrainSample& terrain) const -> float;

  [[nodiscard]] auto config() const -> const WorldGenerationConfig&;

  /// Terrain material sampling for smooth meshers.
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ) const -> TerrainMaterial;
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ,
                                    const glm::vec3& normal) const -> TerrainMaterial;

private:
  TerrainSampler m_terrain;
  SimplexNoise m_densityNoise;
};

} // namespace terrain
