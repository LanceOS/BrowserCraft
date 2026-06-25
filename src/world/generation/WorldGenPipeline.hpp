#pragma once

#include <cstdint>
#include <memory>
#include "SimplexNoise.hpp"
#include "content/biomes/IClimateSource.hpp"
#include "content/biomes/BiomeFactory.hpp"
#include "content/biomes/BiomeSampler.hpp"
#include "CaveCarver.hpp"
#include "OreDistributor.hpp"

namespace voxel {

/// Configuration for world generation noise layering.
/// Terrain height is computed as:
///   height = baseHeight
///          + continental(worldX, worldZ) * continentalAmplitude
///          + detail(worldX, worldZ) * detailAmplitude
///          + blendedHeightBias(worldX, worldZ)
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

/// Full world generation pipeline for a single chunk.
/// Call generate() with a ChunkSlot to fill its voxels, light, and redstone arrays.
///
/// Climate noise is obtained through an IClimateSource interface so that
/// different strategies (Simplex, flat preset, debug) can be swapped in
/// without modifying this class.
class WorldGenPipeline {
public:
  /// Construct with a seed — creates a default BiomeSampler internally.
  explicit WorldGenPipeline(uint32_t seed, const WorldGenerationConfig& config = {});

  /// Construct with an external climate source (non-owning reference).
  /// Allows callers to provide a custom IClimateSource strategy.
  /// The seed parameter is used for terrain noise (continental, detail,
  /// density, cave, ore) independent of the climate source.
  explicit WorldGenPipeline(biome::IClimateSource& climateSource,
                            uint32_t seed,
                            const WorldGenerationConfig& config = {});

  /// Generate terrain into the given voxel array.
  void generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                uint32_t chunkSeed = 0);

  /// Fill a chunk from an acquired slot (used by worker threads).
  void fillChunk(uint8_t* voxels, int32_t* chunkXPtr, int32_t* chunkZPtr,
                 uint32_t* genSeed, int32_t sizeX, int32_t sizeY, int32_t sizeZ);

private:
  /// Multi-octave fractal noise summation (2D convenience).
  /// Evaluates noise3D(x, 0, z * frequency) for each octave.
  [[nodiscard]] static auto fractalNoise2D(const SimplexNoise& noise,
                                            float x, float z,
                                            int octaves,
                                            float lacunarity,
                                            float persistence) -> float;

  /// Owned BiomeSampler (only used when constructed via seed-based ctor).
  std::unique_ptr<biome::BiomeSampler> m_ownedSampler;
  /// Non-owning pointer — points to m_ownedSampler or an external IClimateSource.
  biome::IClimateSource* m_climateSource;

  SimplexNoise m_continentalNoise;
  SimplexNoise m_detailNoise;
  SimplexNoise m_densityNoise;
  CaveCarver m_caveCarver;
  OreDistributor m_oreDist;
  WorldGenerationConfig m_config;
};

} // namespace voxel
