#pragma once

#include <string>
#include <cstdint>

namespace voxel::biome {

struct BiomeSurfaceRule {
  std::string name;
  uint8_t topBlock;
  uint8_t fillerBlock;
  int32_t depth;
  float heightBias;
};

inline constexpr BiomeSurfaceRule PlainsBiome    {"plains",    2, 3, 4,  0.0f};
inline constexpr BiomeSurfaceRule DesertBiome    {"desert",    2, 2, 4, -1.5f};
inline constexpr BiomeSurfaceRule ForestBiome    {"forest",    2, 3, 5,  1.5f};
inline constexpr BiomeSurfaceRule MountainsBiome {"mountains", 1, 1, 2,  8.0f};
inline constexpr BiomeSurfaceRule SwampBiome     {"swamp",     3, 3, 6, -2.0f};

} // namespace voxel::biome
