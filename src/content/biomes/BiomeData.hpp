#pragma once

#include <cstdint>
#include <array>
#include "world/BlockIds.hpp"

namespace voxel::biome {

/// Numeric biome ID — replaces string-based biome name comparisons.
/// Enables O(1) lookups and fully constexpr surface rule definitions.
enum class BiomeId : uint8_t {
  Plains = 0,
  Desert,
  Forest,
  Mountains,
  Swamp,
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

/// Compile-time registry of all biomes for iteration / lookup.
inline constexpr std::array ALL_BIOMES = {
  PlainsBiome,
  DesertBiome,
  ForestBiome,
  MountainsBiome,
  SwampBiome,
};

} // namespace voxel::biome
