#pragma once

#include "GreedyMesher.hpp"
#include <algorithm>
#include <cstdint>

namespace voxel {
namespace mesher {

inline auto voxelIndex(int32_t x, int32_t y, int32_t z,
                       const MesherConfig& cfg) -> int32_t {
  return (y * cfg.sizeZ + z) * cfg.sizeX + x;
}

inline auto skyNibble(uint8_t packed) -> int32_t { return (packed >> 4) & 0x0F; }
inline auto blockNibble(uint8_t packed) -> int32_t { return packed & 0x0F; }

// @see notes/chunk-border-light-seams.md
inline auto getPackedLight(const uint8_t* light,
                           int32_t x, int32_t y, int32_t z,
                           const MesherConfig& cfg) -> uint8_t {
  // Clamp instead of returning 0 so border vertices do not blend in
  // artificial darkness when a chunk has no neighbor data available.
  x = std::clamp(x, 0, cfg.sizeX - 1);
  y = std::clamp(y, 0, cfg.sizeY - 1);
  z = std::clamp(z, 0, cfg.sizeZ - 1);
  return light[voxelIndex(x, y, z, cfg)];
}

// @see notes/chunk-shadow-banding.md
/// Pack sky light, block light, and AO into a float.
/// The shader does `uint(a_lightData + 0.5)` to recover the integer.
inline auto packLight(int32_t sky, int32_t block, int32_t ao) -> float {
  uint32_t p = ( static_cast<uint32_t>(sky) & 0x0Fu)
             | ((static_cast<uint32_t>(block) & 0x0Fu) << 4)
             | ((static_cast<uint32_t>(ao)  & 0x03u) << 16);
  return static_cast<float>(p);
}

struct AvgLight {
  int32_t sky = 0;
  int32_t block = 0;
};

inline auto cornerLight(const uint8_t* light,
                        int32_t axis, int32_t sign,
                        int32_t uAxis, int32_t vAxis,
                        const int32_t corner[3],
                        const MesherConfig& cfg) -> AvgLight {
  const int32_t airAxisCoord = corner[axis] + (sign < 0 ? -1 : 0);

  int32_t skyTotal = 0;
  int32_t blockTotal = 0;
  int32_t count = 0;

  for (int32_t du = -1; du <= 0; ++du) {
    for (int32_t dv = -1; dv <= 0; ++dv) {
      int32_t sample[3] = {corner[0], corner[1], corner[2]};
      sample[axis] = airAxisCoord;
      sample[uAxis] += du;
      sample[vAxis] += dv;

      const uint8_t packed = getPackedLight(light, sample[0], sample[1], sample[2], cfg);
      skyTotal += skyNibble(packed);
      blockTotal += blockNibble(packed);
      ++count;
    }
  }

  AvgLight result;
  if (count > 0) {
    result.sky = (skyTotal + count / 2) / count;
    result.block = (blockTotal + count / 2) / count;
  }
  return result;
}

} // namespace mesher
} // namespace voxel
