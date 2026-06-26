#pragma once

#include <glm/glm.hpp>

namespace voxel {

/// Supported terrain editing brush actions.
enum class BrushType {
  SubtractSphere, // Carves away terrain (increases density)
  AddSphere,      // Adds terrain (decreases density)
  Smooth,         // Averages density with neighbors
  Flatten,        // Projects density toward a target plane
};

/// Configuration parameters for a terrain density edit brush.
struct TerrainBrush {
  BrushType type = BrushType::SubtractSphere;
  glm::vec3 center = glm::vec3(0.0f);
  float radius = 5.0f;
  float strength = 1.0f;
  glm::vec3 planeNormal = glm::vec3(0.0f, 1.0f, 0.0f); // Used by the Flatten brush
};

} // namespace voxel
