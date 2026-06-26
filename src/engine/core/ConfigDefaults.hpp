#pragma once

#include "Config.hpp"

namespace voxel {

/// Default runtime configuration used by the standalone entry point.
inline auto makeDefaultGameConfig() -> GameConfig {
  GameConfig cfg{};
  cfg.chunkSize = 16;
  cfg.worldHeight = 256;
  cfg.renderDistance = 12;
  cfg.useSurfaceNets = false;
  cfg.maxVertsPerChunk = 40000;
  cfg.maxIndicesPerChunk = 60000;
  cfg.vertexStrideFloats = 10;
  cfg.textureArrayLayers = 64;
  return cfg;
}

} // namespace voxel
