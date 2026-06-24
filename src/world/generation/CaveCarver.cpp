#include "CaveCarver.hpp"
#include "SimplexNoise.hpp"
#include <algorithm>

namespace voxel {

CaveCarver::CaveCarver(uint32_t seed)
  : m_noise(std::make_unique<SimplexNoise>(seed ^ 0xcafeu))
  , m_rngState(seed ^ 0xc4e5u) {}

CaveCarver::~CaveCarver() = default;

auto CaveCarver::rng() -> float {
  m_rngState = (m_rngState * 1664525u + 1013904223u);
  return static_cast<float>(m_rngState) / 4294967296.0f;
}

void CaveCarver::carve(uint8_t* voxels, int32_t baseX, int32_t baseZ,
                        int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  constexpr int32_t numWorms = 4;

  for (int32_t worm = 0; worm < numWorms; ++worm) {
    float x = rng() * static_cast<float>(sizeX);
    float y = 10.0f + rng() * 40.0f;
    float z = rng() * static_cast<float>(sizeZ);
    float yaw = rng() * 3.14159265f * 2.0f;
    float pitch = (rng() - 0.5f) * 3.14159265f * 0.5f;
    int32_t length = 40 + static_cast<int32_t>(rng() * 80.0f);

    for (int32_t step = 0; step < length; ++step) {
      float worldX = static_cast<float>(baseX) + x;
      float worldY = y;
      float worldZ = static_cast<float>(baseZ) + z;

      yaw += m_noise->noise3D(worldX * 0.05f, worldY * 0.05f, worldZ * 0.05f) * 0.2f;
      pitch += m_noise->noise3D(worldX * 0.05f + 10.0f, worldY * 0.05f, worldZ * 0.05f) * 0.1f;

      x += std::cos(pitch) * std::cos(yaw);
      y += std::sin(pitch);
      z += std::cos(pitch) * std::sin(yaw);

      float radius = 1.5f + m_noise->noise3D(worldX * 0.1f, worldY * 0.1f, worldZ * 0.1f) * 0.5f;
      carveSphere(voxels, x, y, z, radius, sizeX, sizeY, sizeZ);
    }
  }
}

void CaveCarver::carveSphere(uint8_t* voxels, float cx, float cy, float cz, float radius,
                              int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  int32_t minX = std::max(0, static_cast<int32_t>(std::floor(cx - radius)));
  int32_t maxX = std::min(sizeX - 1, static_cast<int32_t>(std::ceil(cx + radius)));
  int32_t minY = std::max(0, static_cast<int32_t>(std::floor(cy - radius)));
  int32_t maxY = std::min(sizeY - 1, static_cast<int32_t>(std::ceil(cy + radius)));
  int32_t minZ = std::max(0, static_cast<int32_t>(std::floor(cz - radius)));
  int32_t maxZ = std::min(sizeZ - 1, static_cast<int32_t>(std::ceil(cz + radius)));
  float r2 = radius * radius;

  for (int32_t y = minY; y <= maxY; ++y) {
    for (int32_t z = minZ; z <= maxZ; ++z) {
      for (int32_t x = minX; x <= maxX; ++x) {
        float dx = static_cast<float>(x) - cx;
        float dy = static_cast<float>(y) - cy;
        float dz = static_cast<float>(z) - cz;
        if (dx*dx + dy*dy + dz*dz > r2) continue;
        int32_t idx = (y * sizeZ + z) * sizeX + x;
        if (voxels[idx] != 0 && voxels[idx] != 8 && voxels[idx] != 10) {
          voxels[idx] = 0;
        }
      }
    }
  }
}

} // namespace voxel
