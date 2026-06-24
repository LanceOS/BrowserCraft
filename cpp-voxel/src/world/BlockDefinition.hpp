#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace voxel {

/// Axis-aligned bounding box for block collision.
struct BlockAABB {
  float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
  float maxX = 1.0f, maxY = 1.0f, maxZ = 1.0f;

  [[nodiscard]] auto hasVolume() const -> bool {
    return maxX > minX && maxY > minY && maxZ > minZ;
  }
};

inline constexpr BlockAABB FULL_BLOCK_AABB{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
inline constexpr BlockAABB EMPTY_BLOCK_AABB{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

/// Block material properties.
struct BlockMaterial {
  bool opaque = true;
  bool transparent = false;
  bool liquid = false;
  bool foliage = false;
  uint8_t lightEmission = 0;
};

/// Texture indices per face.
struct BlockTextures {
  uint8_t top = 0;
  uint8_t bottom = 0;
  uint8_t side = 0;
};

/// Full block definition registered in the BlockRegistry.
struct BlockDefinition {
  uint16_t id = 0;
  std::string name;
  BlockTextures textures;
  BlockMaterial material;
  BlockAABB collision = FULL_BLOCK_AABB;
};

} // namespace voxel
