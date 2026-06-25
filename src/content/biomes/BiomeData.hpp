#pragma once

#include <string>
#include <cstdint>
#include "world/BlockIds.hpp"

namespace voxel::biome {

struct BiomeSurfaceRule {
  std::string name;
  uint8_t topBlock;
  uint8_t fillerBlock;
  int32_t depth;
  float heightBias;
};

// Plains: grass top, dirt filler, moderate depth, neutral height bias.
inline constexpr BiomeSurfaceRule PlainsBiome    {"plains",    BlockId::GRASS, BlockId::DIRT,  4,  0.0f};
// Desert: sand top, sand filler, shallow depth, slightly depressed.
inline constexpr BiomeSurfaceRule DesertBiome    {"desert",    BlockId::SAND,  BlockId::SAND,  3, -3.0f};
// Forest: grass top, dirt filler, deep soil, mild height boost.
inline constexpr BiomeSurfaceRule ForestBiome    {"forest",    BlockId::GRASS, BlockId::DIRT,  5,  3.0f};
// Mountains: grass/stone top, stone core, thin soil, large height bias.
// The large heightBias combined with steep noise in WorldGenPipeline creates proper slopes.
inline constexpr BiomeSurfaceRule MountainsBiome {"mountains", BlockId::GRASS, BlockId::STONE, 3,  22.0f};
// Swamp: wet stone/mud, depressed and wet.
inline constexpr BiomeSurfaceRule SwampBiome     {"swamp",     BlockId::STONE, BlockId::STONE, 5, -3.0f};

} // namespace voxel::biome
