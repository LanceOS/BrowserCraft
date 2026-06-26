#pragma once

#include <glm/glm.hpp>

namespace terrain {

class World;

struct TerrainRaycastHit {
  bool hit = false;
  glm::vec3 position{0.0f};
  glm::vec3 normal{0.0f};
  float distance = 0.0f;
};

auto raycastTerrain(
    const World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance) -> TerrainRaycastHit;

} // namespace terrain
