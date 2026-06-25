#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "world/BlockDefinition.hpp"

namespace voxel::flora {

/// How this flora is rendered in the mesh.
enum class FloraRenderType : uint8_t {
  /// Full cube with foliage alpha-test (e.g., leaves).
  FOLIAGE_CUBE,
  /// Two intersecting quads forming an X (e.g., flowers, saplings).
  CROSS_QUAD,
  /// Four tall cross-quads for 2-block-high plants (e.g., tall grass, rose bush).
  TALL_CROSS_QUAD,
  /// Single quad on the side of a block (e.g., vines).
  SIDE_ATTACHMENT,
  /// Full cube with custom AABB and no special rendering (e.g., cactus).
  SOLID_CUSTOM_AABB,
};

/// Soil types that a flora can grow on.
enum class SoilType : uint8_t {
  DIRT,
  GRASS,
  SAND,
  FARMLAND,
  ANY_SOLID,
};

/// Growth stages for a flora that matures over time.
enum class GrowthStage : uint8_t {
  STAGE_0,  /// Just planted / initial state.
  STAGE_1,
  STAGE_2,
  STAGE_3,
  MATURE,   /// Fully grown, ready for harvest.
};

/// Light level requirements for growth and survival.
struct LightRequirements {
  uint8_t minSkyLight = 0;    /// Minimum sky light (0-15). 0 = no minimum.
  uint8_t minBlockLight = 0;  /// Minimum block light (0-15). 0 = no minimum.
  uint8_t maxSkyLight = 15;   /// Maximum sky light (0-15). 15 = no maximum.
};

/// Base properties shared by all flora.
struct FloraProperties {
  uint16_t blockId = 0;
  std::string name;
  FloraRenderType renderType = FloraRenderType::FOLIAGE_CUBE;
  std::vector<uint8_t> textureLayers;  /// Texture layer index for each growth stage.
  std::vector<SoilType> acceptableSoil;
  std::vector<std::string> biomeAffinity;  /// Biome names where this flora naturally spawns.
  LightRequirements lightRequirements;
  BlockAABB collision = EMPTY_BLOCK_AABB;  /// Empty for cross-quad plants.
  bool dropsSelf = true;
  uint16_t dropItemId = 0;
  uint8_t dropCountMin = 1;
  uint8_t dropCountMax = 1;
  bool boneMealable = false;
};

} // namespace voxel::flora
