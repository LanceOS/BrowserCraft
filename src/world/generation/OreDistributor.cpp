#include "OreDistributor.hpp"
#include "world/BlockIds.hpp"
#include <cmath>

namespace voxel {

struct OreConfig { uint8_t blockId; int32_t minY, maxY, veinsPerChunk, veinSize; };
static constexpr OreConfig ORE_CONFIGS[] = {
  {BlockId::COAL_ORE, 5, 64, 20, 8},
  {BlockId::IRON_ORE, 5, 32, 12, 6},
  {BlockId::GOLD_ORE, 5, 16,  4, 4},
  // Diamond ore not yet registered in blocks.json — skip for now
};

OreDistributor::OreDistributor(uint32_t) {}

void OreDistributor::distribute(uint8_t* voxels, int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                                uint32_t chunkSeed) {
  uint32_t rngState = chunkSeed ^ 0x0be5u;
  auto rng = [&]() -> float {
    rngState = (rngState * 48271u + 1u);
    return static_cast<float>(rngState) / 4294967296.0f;
  };

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
        // Replace stone or grass with ore
        if (voxels[idx] == BlockId::STONE || voxels[idx] == BlockId::GRASS) voxels[idx] = cfg.blockId;
      }
    }
  }
}

} // namespace voxel
