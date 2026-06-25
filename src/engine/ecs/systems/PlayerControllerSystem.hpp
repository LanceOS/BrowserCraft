#pragma once

#include "engine/ecs/SystemManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/components/Components.hpp"
#include "engine/core/InputState.hpp"
#include "engine/core/Config.hpp"
#include "engine/render/CameraView.hpp"
#include "ui/UIManager.hpp"
#include "game/GameSession.hpp"
#include "world/World.hpp"
#include <GLFW/glfw3.h>

namespace voxel {

class Game;

/// Handles first-person player controls: mouse look, WASD movement,
/// gravity, collision, jumping, flying, and camera synchronisation.
///
/// Reads from InputState and the player's ECS components each frame,
/// writes position/velocity to the RigidBody and Transform, and
/// updates the CameraView for rendering.
class PlayerControllerSystem final : public System<Game> {
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
  void update(Game& state, float dt) override;

public:
  /// Push the player entity at \a entityIndex upward until they are no longer
  /// colliding with blocks.  Called after spawning to prevent the player from
  /// being stuck inside terrain.  \a entityIndex is a raw component-store index
  /// (not an EntityManager ID), obtained from EntityManager::indexOf().
  void pushPlayerOutOfBlocks(int32_t entityIndex);

private:
  void applyMouseLook(float dt);
  void applyMovement(float dt, cmp::Transform& transform, cmp::RigidBody& body, cmp::Player& player, const glm::vec3& moveDir);
  auto collidesAt(const glm::vec3& candidatePosition, const cmp::RigidBody& body) const -> bool;
  auto groundHeightAt(float worldX, float worldZ, int32_t startY) const -> int32_t;
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
  World& m_world;
  CameraView& m_camera;
  const GameConfig& m_config;
  UIManager& m_ui;
  GameSession& m_session;
  bool& m_cameraDirty;
  int32_t m_cachedPlayerIndex = -1;
  bool m_prevOnGround = true; // previous frame's onGround, used for auto-jump on landing. Initialised true so the first frame after spawn doesn't false-trigger justLanded.
};

} // namespace voxel
