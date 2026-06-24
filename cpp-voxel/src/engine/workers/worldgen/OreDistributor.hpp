#pragma once

#include <cstdint>

namespace voxel {

/// Distributes ore veins through generated stone in a chunk.
class OreDistributor {
public:
  explicit OreDistributor(uint32_t seed);

  void distribute(uint8_t* voxels, int32_t sizeX, int32_t sizeY, int32_t sizeZ);

private:
  auto rng() -> float;
  uint32_t m_rngState;
};

} // namespace voxel
