#pragma once

#include <cstdint>

namespace voxel {

/// Runtime configuration for the game engine.
struct GameConfig {
  int32_t chunkSize = 16;
  int32_t worldHeight = 256;
  int32_t renderDistance = 8;
  uint32_t worldSeed = 12345;
  int32_t maxVertsPerChunk = 50000;
  int32_t maxIndicesPerChunk = 100000;
  int32_t vertexStrideFloats = 10;
  int32_t textureArrayLayers = 64;
};

} // namespace voxel
