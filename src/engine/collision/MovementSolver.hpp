#pragma once

#include <glm/glm.hpp>
#include "engine/ecs/components/Components.hpp"

namespace terrain {

class EntityCollisions;
class World;
struct GameConfig;

namespace physics {

class MovementSolver {
public:
  static constexpr float kStepHeight = 0.52f;

  MovementSolver(const EntityCollisions& collisions,
                 World& world,
                 const GameConfig& config,
                 cmp::RigidBody& body);

  void resolve(float dx, float dy, float dz, cmp::Transform& transform);

private:
  auto getGroundHeightFloat(float worldX, float worldZ, float startY) -> float;
  auto highestGroundInFootprintFloat(const glm::vec3& probePos, float startY) -> float;

  const EntityCollisions& m_collisions;
  World& m_world;
  const GameConfig& m_config;
  cmp::RigidBody& m_body;
  int32_t m_chunkSize;
};

} // namespace physics
} // namespace terrain
