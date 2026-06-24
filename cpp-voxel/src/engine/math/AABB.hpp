#pragma once

#include <glm/glm.hpp>

namespace voxel {

/// Axis-aligned bounding box for frustum culling and collision.
struct AABB {
  float minX, minY, minZ;
  float maxX, maxY, maxZ;

  /// Create AABB from chunk coordinates.
  static auto fromChunk(int chunkX, int chunkZ,
                        float sizeX, float sizeY, float sizeZ) -> AABB {
    return {
      static_cast<float>(chunkX) * sizeX,
      0.0f,
      static_cast<float>(chunkZ) * sizeZ,
      static_cast<float>(chunkX) * sizeX + sizeX,
      sizeY,
      static_cast<float>(chunkZ) * sizeZ + sizeZ,
    };
  }
};

} // namespace voxel
