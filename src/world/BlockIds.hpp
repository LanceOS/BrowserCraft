#pragma once

#include <cstdint>

namespace voxel {

/// Symbolic block IDs matching assets/blocks.json.
/// Add new entries here when adding blocks to the JSON definition.
/// If the JSON changes, update these constants to match.
namespace BlockId {
  inline constexpr uint8_t AIR           = 0;
  inline constexpr uint8_t GRASS         = 1;
  inline constexpr uint8_t DIRT          = 2;
  inline constexpr uint8_t STONE         = 3;
  inline constexpr uint8_t SAND          = 4;
  inline constexpr uint8_t OAK_WOOD      = 5;
  inline constexpr uint8_t OAK_PLANKS    = 6;
  inline constexpr uint8_t BEDROCK       = 7;
  inline constexpr uint8_t WATER         = 8;
  inline constexpr uint8_t OAK_LEAVES    = 9;
  inline constexpr uint8_t MOSSY_STONE   = 10;
  inline constexpr uint8_t LAVA          = 11;
  inline constexpr uint8_t DIAMOND_ORE   = 12;
  inline constexpr uint8_t POWERSTONE_ORE = 13;
  inline constexpr uint8_t COAL_ORE      = 14;
  inline constexpr uint8_t IRON_ORE      = 15;
  inline constexpr uint8_t GOLD_ORE      = 16;
  inline constexpr uint8_t TALL_GRASS    = 17;
  inline constexpr uint8_t FERN          = 18;
} // namespace BlockId

} // namespace voxel
