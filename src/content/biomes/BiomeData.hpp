#pragma once

#include <cstdint>
#include <array>
#include "world/BlockIds.hpp"

namespace voxel::biome {

/// A single temperature+humidity sample at a world position.
/// Bundling both values avoids redundant noise evaluations when
/// multiple consumers (sampleBiome, blendedHeightBias, mountainWeight)
/// need the same climate data for the same coordinate.
struct ClimateSample {
  float temperature;
  float humidity;
};

/// Per-biome weights for a given climate sample.
/// All weights are in [0, 1] and sum to 1 after normalisation.
/// Exposed so other systems (flora, mob spawning) can determine which
/// biomes are present at a location without duplicating blending logic.
struct BiomeWeights {
  float plains    = 0.0f;
  float desert    = 0.0f;
  float forest    = 0.0f;
  float mountains = 0.0f;
  float swamp     = 0.0f;
  float ocean     = 0.0f;
};

/// Numeric biome ID — replaces string-based biome name comparisons.
/// Enables O(1) lookups and fully constexpr surface rule definitions.
enum class BiomeId : uint8_t {
  Plains = 0,
  Desert,
  Forest,
  Mountains,
  Swamp,
  Ocean,
};

/// Surface blocks and height bias for a single biome.
struct BiomeSurfaceRule {
  BiomeId id;
  uint8_t topBlock;
  uint8_t fillerBlock;
  int32_t depth;
  float heightBias;
};

// ---------------------------------------------------------------------------
// Named threshold constants — single source of truth.
// Used by BiomeSampler::pick(), blendedHeightBias(), and mountainWeight().
// These are exposed here so all consumers reference the same values.
// ---------------------------------------------------------------------------

inline constexpr float kClimateScale           = 0.008f;

inline constexpr float kMountainTempThreshold  = 0.28f;
inline constexpr float kMountainTransWidth     = 0.08f;

inline constexpr float kDesertTempThreshold    = 0.65f;
inline constexpr float kDesertHumidThreshold   = 0.35f;
inline constexpr float kDesertTransWidth       = 0.08f;

inline constexpr float kSwampHumidThreshold    = 0.72f;
inline constexpr float kSwampHumidTransWidth   = 0.10f;
inline constexpr float kSwampTempThreshold     = 0.35f;
inline constexpr float kSwampTempTransWidth    = 0.08f;

inline constexpr float kForestHumidThreshold   = 0.55f;
inline constexpr float kForestTransWidth       = 0.08f;

inline constexpr float kOceanTempThreshold     = 0.42f;
inline constexpr float kOceanHumidThreshold    = 0.40f;
inline constexpr float kOceanTransWidth        = 0.08f;

// ---------------------------------------------------------------------------
// Biome surface rule definitions
// ---------------------------------------------------------------------------

// Plains: grass top, dirt filler, moderate depth, neutral height bias.
inline constexpr BiomeSurfaceRule PlainsBiome    {BiomeId::Plains,    BlockId::GRASS, BlockId::DIRT,  4,  0.0f};
// Desert: sand top, sand filler, shallow depth, slightly depressed.
inline constexpr BiomeSurfaceRule DesertBiome    {BiomeId::Desert,    BlockId::SAND,  BlockId::SAND,  3, -3.0f};
// Forest: grass top, dirt filler, deep soil, mild height boost.
inline constexpr BiomeSurfaceRule ForestBiome    {BiomeId::Forest,    BlockId::GRASS, BlockId::DIRT,  5,  3.0f};
// Mountains: grass/stone top, stone core, thin soil, large height bias.
// The large heightBias combined with steep noise in WorldGenPipeline creates proper slopes.
inline constexpr BiomeSurfaceRule MountainsBiome {BiomeId::Mountains, BlockId::GRASS, BlockId::STONE, 3,  22.0f};
// Swamp: wet stone/mud, depressed and wet.
inline constexpr BiomeSurfaceRule SwampBiome     {BiomeId::Swamp,     BlockId::STONE, BlockId::STONE, 5, -3.0f};
// Ocean: cold-ish and dry-ish, deep depression to stay below sea level, sand floor.
inline constexpr BiomeSurfaceRule OceanBiome     {BiomeId::Ocean,     BlockId::SAND,  BlockId::SAND,  3, -14.0f};

/// Hermite smoothstep: maps [threshold - width/2, threshold + width/2]
/// to [0, 1] with smooth ease-in-out. Values outside the transition
/// range clamp to 0 or 1 at compile time.
inline constexpr float smoothEdge(float value, float threshold, float width) {
  float edge = (value - threshold + width * 0.5f) / width;
  if (edge < 0.0f) edge = 0.0f;
  if (edge > 1.0f) edge = 1.0f;
  return edge * edge * (3.0f - 2.0f * edge);
}

/// Compile-time registry of all biomes for iteration / lookup.
inline constexpr std::array ALL_BIOMES = {
  PlainsBiome,
  DesertBiome,
  ForestBiome,
  MountainsBiome,
  SwampBiome,
  OceanBiome,
};

} // namespace voxel::biome
