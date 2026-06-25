#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace voxel {

/// 3D Simplex noise implementation for terrain generation.
class SimplexNoise {
public:
  explicit SimplexNoise(uint32_t seed);

  /// 2D noise (calls noise3D with y=0).
  [[nodiscard]] auto noise2D(float x, float z) const -> float {
    return noise3D(x, 0.0f, z);
  }

  /// 3D simplex noise, range approximately [-1, 1].
  [[nodiscard]] auto noise3D(float x, float y, float z) const -> float;

private:
  static constexpr float F3 = 1.0f / 3.0f;
  static constexpr float G3 = 1.0f / 6.0f;

  std::array<uint8_t, 512> m_perm{};
  std::array<uint8_t, 512> m_permMod12{};

  static constexpr float grad3[36] = {
    1,1,0, -1,1,0, 1,-1,0, -1,-1,0,
    1,0,1, -1,0,1, 1,0,-1, -1,0,-1,
    0,1,1, 0,-1,1, 0,1,-1, 0,-1,-1,
  };
};

} // namespace voxel
