#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace voxel {

class World;
namespace cmp {
struct RigidBody;
struct Transform;
}
struct GameConfig;

/// Pure collision-detection and ground-scanning primitives for entities.
/// Extracted from PlayerControllerSystem so collisions can be worked on
/// independently of input, jump, and camera logic.
class EntityCollisions {
public:
  EntityCollisions(World& world, const GameConfig& config);

  /// Test whether \a candidatePosition + \a body AABB overlaps any solid block.
  [[nodiscard]] auto collidesAt(const glm::vec3& candidatePosition,
                                const cmp::RigidBody& body) const -> bool;

  /// Scan downward from \a startY at the integer column (\a worldX, \a worldZ)
  /// and return the Y-coordinate of the highest solid block, or -1 if none.
  [[nodiscard]] auto groundHeightAt(float worldX, float worldZ,
                                    int32_t startY) const -> int32_t;

  /// Is the position (in world-block coordinates) a fluid?
  [[nodiscard]] auto isFluidAt(float worldX, float worldY,
                               float worldZ) const -> bool;

  /// True when at least one chunk has been generated and had its mesh built.
  [[nodiscard]] auto hasTerrain() const -> bool;

  /// Push the entity upward by small increments until it stops colliding.
  void pushOutOfBlocks(glm::vec3& position, const cmp::RigidBody& body) const;

  /// Resolve one frame of entity movement with per-axis sub-step collision.
  /// \a dx, \a dy, \a dz are this frame's intended displacements (after
  /// gravity, drag, and input).
  void resolveMovement(float dx, float dy, float dz,
                       cmp::Transform& transform,
                       cmp::RigidBody& body) const;

private:
  World& m_world;
  const GameConfig& m_config;
};

} // namespace voxel
