#pragma once

#include "GreedyMesher.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel::mesher {

inline constexpr uint8_t MAX_LIGHT_LEVEL = 15u;

inline auto packVoxelLight(uint8_t sky, uint8_t block) -> uint8_t {
  return static_cast<uint8_t>(((sky & 0x0Fu) << 4) | (block & 0x0Fu));
}

inline auto skyLightNibble(uint8_t packed) -> uint8_t {
  return static_cast<uint8_t>((packed >> 4) & 0x0Fu);
}

inline auto blockLightNibble(uint8_t packed) -> uint8_t {
  return static_cast<uint8_t>(packed & 0x0Fu);
}

inline auto isLightPassable(uint8_t blockId, const BlockRegistry& blocks) -> bool {
  if (blockId == 0) return true;
  const auto* def = blocks.tryGet(blockId);
  if (!def) return false;
  return !def->material.opaque;
}

inline auto emittedLight(uint8_t blockId, const BlockRegistry& blocks) -> uint8_t {
  if (blockId == 0) return 0u;
  const auto* def = blocks.tryGet(blockId);
  if (!def) return 0u;
  return static_cast<uint8_t>(std::min<uint8_t>(def->material.lightEmission, MAX_LIGHT_LEVEL));
}

inline void calculateLighting(const uint8_t* voxels,
                              uint8_t* light,
                              const BlockRegistry& blocks,
                              const MesherConfig& cfg) {
  if (!voxels || !light) return;
  if (cfg.sizeX <= 0 || cfg.sizeY <= 0 || cfg.sizeZ <= 0) return;

  const int32_t sizeX = cfg.sizeX;
  const int32_t sizeY = cfg.sizeY;
  const int32_t sizeZ = cfg.sizeZ;
  const auto volume =
    static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY) * static_cast<size_t>(sizeZ);

  struct LightNode {
    int32_t x;
    int32_t y;
    int32_t z;
  };

  std::vector<LightNode> queue;
  queue.reserve(volume);

  for (int32_t z = 0; z < sizeZ; ++z) {
    for (int32_t x = 0; x < sizeX; ++x) {
      uint8_t skyLevel = MAX_LIGHT_LEVEL;
      for (int32_t y = sizeY - 1; y >= 0; --y) {
        const int32_t index = (y * sizeZ + z) * sizeX + x;
        const uint8_t blockId = voxels[index];
        const uint8_t emitted = emittedLight(blockId, blocks);

        if (blockId != 0 && !isLightPassable(blockId, blocks)) {
          skyLevel = 0u;
        }

        light[index] = packVoxelLight(skyLevel, emitted);
        if (emitted > 1u) {
          queue.push_back(LightNode{x, y, z});
        }
      }
    }
  }

  static constexpr int32_t kNeighborOffsets[6][3] = {
    { 1, 0, 0}, {-1, 0, 0},
    { 0, 1, 0}, { 0,-1, 0},
    { 0, 0, 1}, { 0, 0,-1},
  };

  for (size_t head = 0; head < queue.size(); ++head) {
    const LightNode node = queue[head];
    const int32_t index = (node.y * sizeZ + node.z) * sizeX + node.x;
    const uint8_t currentBlockLight = blockLightNibble(light[index]);
    if (currentBlockLight <= 1u) continue;

    const uint8_t nextLight = static_cast<uint8_t>(currentBlockLight - 1u);
    for (const auto& offset : kNeighborOffsets) {
      const int32_t nx = node.x + offset[0];
      const int32_t ny = node.y + offset[1];
      const int32_t nz = node.z + offset[2];
      if (nx < 0 || nx >= sizeX) continue;
      if (ny < 0 || ny >= sizeY) continue;
      if (nz < 0 || nz >= sizeZ) continue;

      const int32_t neighborIndex = (ny * sizeZ + nz) * sizeX + nx;
      const uint8_t neighborBlockId = voxels[neighborIndex];
      if (neighborBlockId != 0 && !isLightPassable(neighborBlockId, blocks)) {
        continue;
      }

      const uint8_t neighborPacked = light[neighborIndex];
      if (blockLightNibble(neighborPacked) >= nextLight) continue;

      light[neighborIndex] = packVoxelLight(skyLightNibble(neighborPacked), nextLight);
      queue.push_back(LightNode{nx, ny, nz});
    }
  }
}

} // namespace voxel::mesher
