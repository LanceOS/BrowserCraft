#pragma once

#include <cstdint>

#include "TerrainSampling.hpp"
#include "CaveCarver.hpp"
#include "OreDistributor.hpp"
#include "SimplexNoise.hpp"

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

/// Full world generation pipeline for a single chunk.
/// Terrain sampling is delegated to TerrainSampler so the continuous surface
/// logic can be reused by smooth meshers without changing the legacy voxel
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

  /// Generate terrain into the given voxel array.
  void generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                uint32_t chunkSeed = 0);

  /// Continuous signed-distance-like sampling for smooth meshers.
  [[nodiscard]] auto sampleDensity(float worldX, float worldY, float worldZ) const -> float;

  /// Terrain material sampling for smooth meshers.
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ) const -> TerrainMaterial;
  [[nodiscard]] auto sampleMaterial(float worldX, float worldY, float worldZ,
                                    const glm::vec3& normal) const -> TerrainMaterial;

  /// Fill a chunk from an acquired slot (used by worker threads).
  void fillChunk(uint8_t* voxels, int32_t* chunkXPtr, int32_t* chunkZPtr,
                 uint32_t* genSeed, int32_t sizeX, int32_t sizeY, int32_t sizeZ);

private:
  TerrainSampler m_terrain;
  SimplexNoise m_densityNoise;
  CaveCarver m_caveCarver;
  OreDistributor m_oreDist;
};

} // namespace voxel
