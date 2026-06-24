#include "SimplexNoise.hpp"

namespace voxel {

SimplexNoise::SimplexNoise(uint32_t seed) {
  // Seed-based permutation table generation
  std::array<uint8_t, 256> p{};
  for (int i = 0; i < 256; ++i) p[i] = static_cast<uint8_t>(i);

  uint32_t s = seed;
  auto rand = [&]() -> float {
    s = (s + 0x6d2b79f5u);
    uint32_t t = (s ^ (s >> 15)) * (1u | s);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return static_cast<float>(t ^ (t >> 14)) / 4294967296.0f;
  };

  for (int i = 255; i > 0; --i) {
    int j = static_cast<int>(rand() * (i + 1));
    std::swap(p[i], p[j]);
  }

  for (int i = 0; i < 512; ++i) {
    m_perm[i] = p[i & 255];
    m_permMod12[i] = m_perm[i] % 12;
  }
}

auto SimplexNoise::noise3D(float x, float y, float z) const -> float {
  const auto& perm = m_perm;
  const auto& permMod12 = m_permMod12;

  float skew = (x + y + z) * F3;
  int i = static_cast<int>(std::floor(x + skew));
  int j = static_cast<int>(std::floor(y + skew));
  int k = static_cast<int>(std::floor(z + skew));

  float unskew = static_cast<float>(i + j + k) * G3;
  float x0 = x - (static_cast<float>(i) - unskew);
  float y0 = y - (static_cast<float>(j) - unskew);
  float z0 = z - (static_cast<float>(k) - unskew);

  int i1 = 0, j1 = 0, k1 = 0, i2 = 0, j2 = 0, k2 = 0;

  if (x0 >= y0) {
    if (y0 >= z0)      { i1=1; i2=1; j2=1; }
    else if (x0 >= z0) { i1=1; i2=1; k2=1; }
    else               { k1=1; i2=1; k2=1; }
  } else if (y0 < z0)  { k1=1; j2=1; k2=1; }
  else if (x0 < z0)    { j1=1; j2=1; k2=1; }
  else                 { j1=1; i2=1; j2=1; }

  float x1 = x0 - static_cast<float>(i1) + G3;
  float y1 = y0 - static_cast<float>(j1) + G3;
  float z1 = z0 - static_cast<float>(k1) + G3;
  float x2 = x0 - static_cast<float>(i2) + 2.0f * G3;
  float y2 = y0 - static_cast<float>(j2) + 2.0f * G3;
  float z2 = z0 - static_cast<float>(k2) + 2.0f * G3;
  float x3 = x0 - 1.0f + 3.0f * G3;
  float y3 = y0 - 1.0f + 3.0f * G3;
  float z3 = z0 - 1.0f + 3.0f * G3;

  int ii = i & 255, jj = j & 255, kk = k & 255;

  float n0 = 0, n1 = 0, n2 = 0, n3 = 0;

  float t0 = 0.6f - x0*x0 - y0*y0 - z0*z0;
  if (t0 > 0) {
    int gi0 = permMod12[ii + perm[jj + perm[kk]]] * 3;
    t0 *= t0;
    n0 = t0 * t0 * (grad3[gi0]*x0 + grad3[gi0+1]*y0 + grad3[gi0+2]*z0);
  }

  float t1 = 0.6f - x1*x1 - y1*y1 - z1*z1;
  if (t1 > 0) {
    int gi1 = permMod12[ii+i1 + perm[jj+j1 + perm[kk+k1]]] * 3;
    t1 *= t1;
    n1 = t1 * t1 * (grad3[gi1]*x1 + grad3[gi1+1]*y1 + grad3[gi1+2]*z1);
  }

  float t2 = 0.6f - x2*x2 - y2*y2 - z2*z2;
  if (t2 > 0) {
    int gi2 = permMod12[ii+i2 + perm[jj+j2 + perm[kk+k2]]] * 3;
    t2 *= t2;
    n2 = t2 * t2 * (grad3[gi2]*x2 + grad3[gi2+1]*y2 + grad3[gi2+2]*z2);
  }

  float t3 = 0.6f - x3*x3 - y3*y3 - z3*z3;
  if (t3 > 0) {
    int gi3 = permMod12[ii+1 + perm[jj+1 + perm[kk+1]]] * 3;
    t3 *= t3;
    n3 = t3 * t3 * (grad3[gi3]*x3 + grad3[gi3+1]*y3 + grad3[gi3+2]*z3);
  }

  return 32.0f * (n0 + n1 + n2 + n3);
}

} // namespace voxel
