#pragma once

#include "TerrainBrush.hpp"

namespace voxel {

class World;
class WorldGenPipeline;

/// Public API for performing brush-based density field modifications on the terrain.
class TerrainEditAPI {
public:
  /// Modifies the density field of the world using the specified brush,
  /// automatically identifying intersecting chunks, updating their density fields,
  /// marking them dirty, and triggering remeshing.
  static void applyBrush(World& world, const WorldGenPipeline& pipeline, const TerrainBrush& brush);
};

} // namespace voxel
