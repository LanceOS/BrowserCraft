#pragma once

#include <cstdint>
#include <cmath>
#include <memory>

namespace voxel {

class SimplexNoise;

/// Carves cave tunnels through generated terrain using 3D noise-walk worms.
class CaveCarver {
public:
  explicit CaveCarver(uint32_t seed);

  /// Carve caves into voxel data. Operates on a single chunk.
  void carve(uint8_t* voxels, int32_t baseX, int32_t baseZ,
             int32_t sizeX, int32_t sizeY, int32_t sizeZ);

private:
  void carveSphere(uint8_t* voxels, float cx, float cy, float cz, float radius,
                   int32_t sizeX, int32_t sizeY, int32_t sizeZ);

  std::unique_ptr<SimplexNoise> m_noise;
  uint32_t m_rngState;
  auto rng() -> float;
};

} // namespace voxel
