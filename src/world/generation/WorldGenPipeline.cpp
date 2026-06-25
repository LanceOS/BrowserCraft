#include "WorldGenPipeline.hpp"
#include "world/BlockIds.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace voxel {

WorldGenPipeline::WorldGenPipeline(uint32_t seed, const WorldGenerationConfig& config)
  : m_continentalNoise(seed ^ 0x1a2b3cu),
    m_detailNoise(seed ^ 0x4d5e6fu),
    m_densityNoise(seed),
    m_biomeSampler(seed),
    m_caveCarver(seed),
    m_oreDist(seed),
    m_config(config)
{}

// ---------------------------------------------------------------------------
// Multi-octave fractal noise helper
// ---------------------------------------------------------------------------
auto WorldGenPipeline::fractalNoise2D(const SimplexNoise& noise,
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

// ---------------------------------------------------------------------------
// Main terrain generation
// ---------------------------------------------------------------------------
void WorldGenPipeline::generate(uint8_t* voxels, int32_t chunkX, int32_t chunkZ,
                                 int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                                 uint32_t chunkSeed) {
  const int32_t baseX = chunkX * sizeX;
  const int32_t baseZ = chunkZ * sizeZ;
  const auto& cfg = m_config;
  const int32_t terrainMaxY = sizeY - 2;

  // Pre-compute the biome-name string lookup once per block to avoid
  // calling rule.name every time.  We use a simple boolean per column.
  // (rule.name comparisons are O(strlen) — avoid them in inner loops.)

  for (int32_t z = 0; z < sizeZ; ++z) {
    for (int32_t x = 0; x < sizeX; ++x) {
      const float worldX = static_cast<float>(baseX + x);
      const float worldZ = static_cast<float>(baseZ + z);

      // ---- 1. Continental (large-scale) height ----
      // Use 3 octaves of the dedicated continental noise to get smooth
      // land/sea shapes with natural-looking variation.
      float continental = fractalNoise2D(
          m_continentalNoise,
          worldX * cfg.continentalScale,
          worldZ * cfg.continentalScale,
          3,          // octaves
          2.0f,       // lacunarity
          0.5f);      // persistence

      // ---- 2. Regional / detail height ----
      float detail = fractalNoise2D(
          m_detailNoise,
          worldX * cfg.detailScale,
          worldZ * cfg.detailScale,
          2,          // octaves
          2.0f,
          0.5f);

      // ---- 3. Biome blending ----
      const auto& rule = m_biomeSampler.sampleBiome(worldX, worldZ);
      float heightBias = m_biomeSampler.blendedHeightBias(worldX, worldZ);

      // ---- 4. Mountain amplification ----
      // Use smooth mountain weight (derived from temperature) instead of a
      // hard biome-ID check. This prevents height discontinuities at biome
      // boundaries while still concentrating amplification in cold regions.
      float mountainWeight = m_biomeSampler.mountainWeight(worldX, worldZ);
      float mountainExtra = mountainWeight * fractalNoise2D(
          m_continentalNoise,
          worldX * cfg.mountainScale,
          worldZ * cfg.mountainScale,
          3,      // octaves
          2.5f,   // lacunarity
          0.6f)   // persistence
          * cfg.mountainAmplitude;
      // Keep only upward shaping (negative noise inverted and damped).
      if (mountainExtra < 0.0f) mountainExtra *= -0.6f;

      // ---- 5. Compute final surface height ----
      int32_t surfaceY = static_cast<int32_t>(
          cfg.baseHeight
          + continental * cfg.continentalAmplitude
          + detail * cfg.detailAmplitude
          + heightBias
          + mountainExtra);
      surfaceY = std::clamp(surfaceY, 1, terrainMaxY);

      const bool isDesert = (rule.id == biome::BiomeId::Desert);

      // ---- 6. Fill the column ----
      for (int32_t y = 0; y < sizeY; ++y) {
        const int32_t index = (y * sizeZ + z) * sizeX + x;

        if (y == 0) {
          voxels[index] = BlockId::BEDROCK;
          continue;
        }

        // Above surface
        if (y > surfaceY) {
          voxels[index] = (y <= cfg.seaLevel && !isDesert) ? BlockId::WATER : BlockId::AIR;
          continue;
        }

        // ---- 3D density carve-out (underground cavities / overhangs) ----
        // The threshold increases with depth so cavities only form well
        // below the surface, preventing jagged surface terrain.
        float depthFactor = static_cast<float>(surfaceY - y) * cfg.densityDepthScale;
        float noise3D = m_densityNoise.noise3D(
            worldX * cfg.densityNoiseScale,
            static_cast<float>(y) * cfg.densityNoiseScale,
            worldZ * cfg.densityNoiseScale);

        // Only carve when we're at least 4 blocks below the surface.
        if (noise3D + depthFactor < 0.0f && y < surfaceY - 4) {
          voxels[index] = 0; // air pocket
          continue;
        }

        // ---- Surface layering ----
        if (y == surfaceY) {
          voxels[index] = rule.topBlock;
        } else if (y > surfaceY - rule.depth) {
          voxels[index] = rule.fillerBlock;
        } else {
          voxels[index] = BlockId::STONE;
        }
      }
    }
  }

  // ---- Post-processing ----
  m_caveCarver.carve(voxels, baseX, baseZ, sizeX, sizeY, sizeZ, chunkSeed);
  m_oreDist.distribute(voxels, sizeX, sizeY, sizeZ, chunkSeed);
}

void WorldGenPipeline::fillChunk(uint8_t* voxels, int32_t* chunkXPtr, int32_t* chunkZPtr,
                                  uint32_t* genSeed, int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  int32_t cx = *chunkXPtr;
  int32_t cz = *chunkZPtr;
  generate(voxels, cx, cz, sizeX, sizeY, sizeZ, genSeed ? *genSeed : 0);
}

} // namespace voxel
