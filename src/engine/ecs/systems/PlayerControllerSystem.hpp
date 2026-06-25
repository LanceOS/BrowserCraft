#pragma once

#include "engine/ecs/SystemManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/components/Components.hpp"
#include "engine/core/TickContext.hpp"
#include "engine/core/InputState.hpp"
#include "engine/core/Config.hpp"
#include "engine/render/CameraView.hpp"
#include "engine/collision/EntityCollisions.hpp"
#include "ui/UIManager.hpp"
#include "game/GameSession.hpp"
#include <GLFW/glfw3.h>

namespace voxel {

/// Handles first-person player controls: mouse look, WASD movement,
/// gravity, collision, jumping, flying, and camera synchronisation.
///
/// Reads from InputState and the player's ECS components each frame,
/// writes position/velocity to the RigidBody and Transform, and
/// updates the CameraView for rendering.
class PlayerControllerSystem final : public System {
public:
  PlayerControllerSystem(
    GLFWwindow* window,
    InputState& input,
    ComponentStore<cmp::Transform>& transforms,
    ComponentStore<cmp::RigidBody>& bodies,
    ComponentStore<cmp::Player>& players,
    World& world,
    CameraView& camera,
    const GameConfig& config,
    UIManager& ui,
    GameSession& session,
    bool& cameraDirty);

  [[nodiscard]] auto name() const -> const std::string& override;
  [[nodiscard]] auto stage() const -> SystemStage override;
  void update(TickContext& ctx) override;

  /// Collision engine shared with PlayerSpawnSystem.
  [[nodiscard]] auto collisions() -> EntityCollisions& { return m_collisions; }
  [[nodiscard]] auto collisions() const -> const EntityCollisions& { return m_collisions; }

private:
  void applyMouseLook(float dt);
  void applyMovement(float dt, cmp::Transform& transform, cmp::RigidBody& body, cmp::Player& player, const glm::vec3& moveDir);
  void syncCameraFromPlayer();
  void handleInventoryToggle();

  static constexpr float kMouseSensitivity = 0.0025f;
  static constexpr float kMaxPitch = 1.553343f; // ~89 degrees
  static constexpr float kJumpSpeed = 8.0f;
  static constexpr float kSwimSpeed = 3.5f;

  GLFWwindow* m_window;
  InputState& m_input;
  ComponentStore<cmp::Transform>& m_transforms;
  ComponentStore<cmp::RigidBody>& m_bodies;
  ComponentStore<cmp::Player>& m_players;
  CameraView& m_camera;
  UIManager& m_ui;
  GameSession& m_session;
  bool& m_cameraDirty;
  int32_t m_cachedPlayerIndex = -1;
  bool m_prevOnGround = true; // previous frame's grounded state (from proximity check, not body.onGround). Used for auto-jump on landing. Initialised true so the first frame after spawn doesn't false-trigger justLanded.
  EntityCollisions m_collisions;
};

} // namespace voxel
