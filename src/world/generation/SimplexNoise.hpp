#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

// @deprecated Legacy terrain-world code retained during the render-only migration to triangle meshes.
namespace terrain {

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

  /// Helper for 2D fractal noise (fBm).
  [[nodiscard]] auto fractalNoise2D(float x, float z, int octaves, float lacunarity, float persistence) const -> float {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves; ++i) {
      value += amplitude * noise3D(x * frequency, 0.0f, z * frequency);
      maxAmplitude += amplitude;
      frequency *= lacunarity;
      amplitude *= persistence;
    }

    return maxAmplitude > 0.0f ? value / maxAmplitude : 0.0f;
  }

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

} // namespace terrain
