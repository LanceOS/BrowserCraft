#pragma once

#include <cstdint>

namespace voxel {

/// Runtime configuration for the game engine.
struct GameConfig {
  int32_t chunkSize = 16;
  int32_t worldHeight = 256;
  int32_t renderDistance = 12;
  uint32_t worldSeed = 0;
  int32_t maxVertsPerChunk = 15000;
  int32_t maxIndicesPerChunk = 30000;
  int32_t vertexStrideFloats = 10;
  int32_t textureArrayLayers = 64;
};

} // namespace voxel
