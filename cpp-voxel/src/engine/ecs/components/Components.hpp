#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace voxel::cmp {

/// Position, rotation, scale.
struct Transform {
  glm::vec3 position{0.0f};
  float yaw = 0.0f;
  float pitch = 0.0f;
  float scale = 1.0f;
};

/// Player-specific data.
struct Player {
  float yaw = 0.0f;
  float pitch = 0.0f;
  float eyeHeight = 1.62f;
  float walkSpeed = 4.317f;
  float sprintSpeed = 6.0f;
  float flySpeed = 10.0f;
  uint8_t isFlying = 0;
  uint8_t selectedHotbarSlot = 0;
};

/// Physics rigid body.
struct RigidBody {
  glm::vec3 velocity{0.0f};
  glm::vec3 aabbMin{-0.3f, 0.0f, -0.3f};
  glm::vec3 aabbMax{0.3f, 1.8f, 0.3f};
  float gravity = 25.0f;
  float drag = 0.91f;
  uint8_t onGround = 0;
  uint8_t isFluid = 0;
};

/// Mob stats.
struct MobStats {
  float width = 0.6f;
  float height = 1.8f;
  float eyeHeight = 1.62f;
  float moveSpeed = 1.0f;
  float attackDamage = 2.0f;
  float maxHealth = 20.0f;
  uint32_t modelId = 0;
};

/// Health component.
struct Health {
  float current = 20.0f;
  float max = 20.0f;
};

} // namespace voxel::cmp
