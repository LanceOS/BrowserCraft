#include "OreDistributor.hpp"
#include <cmath>

namespace voxel {

struct OreConfig { uint8_t blockId; int32_t minY, maxY, veinsPerChunk, veinSize; };
static constexpr OreConfig ORE_CONFIGS[] = {
  {16, 5, 64, 20, 8},  // coal
  {15, 5, 32, 10, 6},  // iron
  {14, 5, 16,  4, 4},  // gold
  {56, 5, 12,  2, 4},  // diamond
};

OreDistributor::OreDistributor(uint32_t seed) : m_rngState(seed ^ 0x0be5u) {}

auto OreDistributor::rng() -> float {
  m_rngState = (m_rngState * 48271u + 1u);
  return static_cast<float>(m_rngState) / 4294967296.0f;
}

void OreDistributor::distribute(uint8_t* voxels, int32_t sizeX, int32_t sizeY, int32_t sizeZ) {
  for (const auto& cfg : ORE_CONFIGS) {
    for (int32_t vein = 0; vein < cfg.veinsPerChunk; ++vein) {
      int32_t x = static_cast<int32_t>(rng() * static_cast<float>(sizeX));
      int32_t y = cfg.minY + static_cast<int32_t>(rng() * static_cast<float>(cfg.maxY - cfg.minY));
      int32_t z = static_cast<int32_t>(rng() * static_cast<float>(sizeZ));

      for (int32_t step = 0; step < cfg.veinSize; ++step) {
        x += rng() > 0.5f ? 1 : -1;
        y += rng() > 0.5f ? 1 : -1;
        z += rng() > 0.5f ? 1 : -1;
        if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) continue;
        int32_t idx = (y * sizeZ + z) * sizeX + x;
        if (voxels[idx] == 1) voxels[idx] = cfg.blockId;
      }
    }
  }
}

} // namespace voxel
