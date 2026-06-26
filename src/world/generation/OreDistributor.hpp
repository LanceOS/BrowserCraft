#pragma once

#include <cstdint>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

/// Distributes ore veins through generated stone in a chunk.
class OreDistributor {
public:
  explicit OreDistributor(uint32_t seed);

  void distribute(uint8_t* voxels, int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                  uint32_t chunkSeed);

private:
};

} // namespace voxel
