#pragma once

#include <cstdint>

namespace terrain {

/// Runtime configuration for the game engine.
struct GameConfig {
  int32_t chunkSize = 16;
  int32_t worldHeight = 256;
  int32_t renderDistance = 12;
  uint32_t worldSeed = 0;
  // These defaults size the persistent chunk-mesh allocator budget.
  // Measured terrain near spawn regularly exceeds the old 15k / 30k values,
  // which caused chunks to drop out and expose hard chunk walls.
  int32_t maxVertsPerChunk = 40000;
  int32_t maxIndicesPerChunk = 60000;
  int32_t vertexStrideFloats = 10;
  int32_t textureArrayLayers = 64;
};

} // namespace terrain
