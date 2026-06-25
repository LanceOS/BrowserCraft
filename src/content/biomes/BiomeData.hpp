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

inline constexpr BiomeSurfaceRule PlainsBiome    {"plains",    BlockId::DIRT,  BlockId::STONE, 4,  0.0f};
inline constexpr BiomeSurfaceRule DesertBiome    {"desert",    BlockId::SAND,  BlockId::SAND,  4, -1.5f};
inline constexpr BiomeSurfaceRule ForestBiome    {"forest",    BlockId::DIRT,  BlockId::STONE, 5,  1.5f};
inline constexpr BiomeSurfaceRule MountainsBiome {"mountains", BlockId::GRASS, BlockId::GRASS, 2,  8.0f};
inline constexpr BiomeSurfaceRule SwampBiome     {"swamp",     BlockId::STONE, BlockId::STONE, 6, -2.0f};

} // namespace voxel::biome
