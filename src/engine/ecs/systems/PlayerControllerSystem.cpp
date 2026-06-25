#include "PlayerControllerSystem.hpp"
#include "engine/core/TickContext.hpp"
#include "engine/ecs/EntityManager.hpp"
#include <algorithm>
#include <cmath>

namespace voxel {

PlayerControllerSystem::PlayerControllerSystem(
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
    bool& cameraDirty)
  : m_window(window)
  , m_input(input)
  , m_transforms(transforms)
  , m_bodies(bodies)
  , m_players(players)
  , m_camera(camera)
  , m_ui(ui)
  , m_session(session)
  , m_cameraDirty(cameraDirty)
  , m_collisions(world, config)
{}

auto PlayerControllerSystem::name() const -> const std::string& {
  static const std::string kName = "PlayerController";
  return kName;
}

auto PlayerControllerSystem::stage() const -> SystemStage {
  return SystemStage::Physics;
}

void PlayerControllerSystem::update(TickContext& ctx) {
  // Only run controls while actively playing
  if (m_session.state() != GameState::InGame &&
      m_session.state() != GameState::GeneratingWorld) {
    return;
  }

  // Resolve player entity index from the TickContext (always fresh).
  int32_t playerId = ctx.playerEntityId;
  if (playerId < 0) return;
  int32_t idx = EntityManager::indexOf(playerId);
  if (idx < 0 ||
      !m_transforms.has(idx) || !m_bodies.has(idx) || !m_players.has(idx)) {
    m_cachedPlayerIndex = -1;
    return;
  }
  m_cachedPlayerIndex = idx;

  handleInventoryToggle();

  auto& transform = m_transforms.get(idx);
  auto& body = m_bodies.get(idx);
  auto& player = m_players.get(idx);

  const bool canControl = (m_session.state() == GameState::InGame ||
                          m_session.state() == GameState::GeneratingWorld) &&
    (m_ui.state() == UIState::InGame) &&
    !m_ui.isInventoryOpen();

  if (!canControl) {
    body.velocity.x = 0.0f;
    body.velocity.z = 0.0f;
    syncCameraFromPlayer();
    return;
  }

  // Ensure pointer lock is active
  if (glfwGetInputMode(m_window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  }
  m_input.pointerLocked = true;

  applyMouseLook(ctx.dt);

  // Build movement direction from WASD
  glm::vec3 forwardFlat(std::sin(player.yaw), 0.0f, -std::cos(player.yaw));
  forwardFlat = glm::normalize(forwardFlat);
  glm::vec3 rightFlat = glm::normalize(glm::cross(forwardFlat, m_camera.up));

  glm::vec3 moveDir(0.0f);
  if (m_input.isHeld(InputState::KEY_W)) moveDir += forwardFlat;
  if (m_input.isHeld(InputState::KEY_S)) moveDir -= forwardFlat;
  if (m_input.isHeld(InputState::KEY_A)) moveDir -= rightFlat;
  if (m_input.isHeld(InputState::KEY_D)) moveDir += rightFlat;

  if (glm::dot(moveDir, moveDir) > 0.0f) {
    moveDir = glm::normalize(moveDir);
  }

  if (!m_collisions.hasTerrain()) {
    // No terrain yet — allow mouse look and horizontal movement but no gravity
    float speed = m_input.isHeld(InputState::KEY_SHIFT) ? player.sprintSpeed
                                                        : player.walkSpeed;
    transform.position += moveDir * speed * ctx.dt;
    body.velocity = glm::vec3(0.0f);
    syncCameraFromPlayer();
    return;
  }

  applyMovement(ctx.dt, transform, body, player, moveDir);
  syncCameraFromPlayer();
}

// ---- Private helpers ----

void PlayerControllerSystem::handleInventoryToggle() {
  if (m_input.isPressed(InputState::KEY_E)) {
    bool nowOpen = !m_ui.isInventoryOpen();
    m_ui.setInventoryOpen(nowOpen);
    glfwSetInputMode(m_window, GLFW_CURSOR,
      nowOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  }
}

void PlayerControllerSystem::applyMouseLook(float /*dt*/) {
  int32_t idx = m_cachedPlayerIndex;
  if (idx < 0 || !m_players.has(idx)) return;
  auto& player = m_players.get(idx);
  player.yaw += m_input.mouseDX() * kMouseSensitivity;
  player.pitch -= m_input.mouseDY() * kMouseSensitivity;
  player.pitch = std::clamp(player.pitch, -kMaxPitch, kMaxPitch);
}

void PlayerControllerSystem::applyMovement(
    float dt,
    cmp::Transform& transform,
    cmp::RigidBody& body,
    cmp::Player& player,
    const glm::vec3& moveDir)
{
  if (player.isFlying) {
    float speed = player.flySpeed;
    if (m_input.isHeld(InputState::KEY_SPACE)) {
      transform.position.y += speed * dt;
    }
    if (m_input.isHeld(InputState::KEY_CTRL)) {
      transform.position.y -= speed * dt;
    }
    transform.position += moveDir * speed * dt;
    body.velocity = glm::vec3(0.0f);
    body.onGround = 0;
    return;
  }

  // Grounded / falling movement
  float speed = m_input.isHeld(InputState::KEY_SHIFT) ? player.sprintSpeed
                                                      : player.walkSpeed;

  // Check ground contact every tick (60 Hz).  We use a fresh proximity scan
  // instead of relying on body.onGround, because body.onGround can be
  // spuriously zeroed by sub-step iteration quirks (step-assist raising,
  // overshoot, etc.) even when the player is visually on the surface.
  int32_t gY = m_collisions.groundHeightAt(
      transform.position.x, transform.position.z,
      std::max(0, static_cast<int32_t>(
          std::floor(transform.position.y + body.aabbMin.y))));
  bool grounded = false;
  if (gY >= 0) {
    float feetY = transform.position.y + body.aabbMin.y;
    float surfY = static_cast<float>(gY + 1);
    grounded = (feetY - surfY) <= 0.55f; // covers kStepHeight (0.52) so step-assist raising doesn't prevent jump
  }

  // Jump trigger: tap Space (isPressed) for a single jump, or hold Space
  // and auto-jump when the player next lands (grounded transition).
  bool spaceHeld = m_input.isHeld(InputState::KEY_SPACE);
  bool justLanded = grounded && !m_prevOnGround;
  if (spaceHeld && (grounded || body.isFluid) &&
      (m_input.isPressed(InputState::KEY_SPACE) || justLanded)) {
    body.velocity.y = body.isFluid ? kSwimSpeed : kJumpSpeed;
    body.onGround = 0;
    body.isFluid = 0;
  }
  // Save grounded state (not body.onGround) for next frame's transition
  // detection.  body.onGround can be stale after sub-step overrides; our
  // own proximity check is trustworthy.
  m_prevOnGround = grounded;

  // Apply gravity
  body.velocity.y -= body.gravity * dt;

  // Apply drag to horizontal velocity before clearing it (drag affects
  // residual velocity from external forces like knockback).
  body.velocity.x *= body.drag;
  body.velocity.z *= body.drag;

  // Clear horizontal velocity — it's set per-frame from input
  body.velocity.x = 0.0f;
  body.velocity.z = 0.0f;

  // Compute total displacement for each axis
  float dx = moveDir.x * speed * dt;
  float dz = moveDir.z * speed * dt;
  float dy = body.velocity.y * dt;

  // Delegate all collision resolution to EntityCollisions
  m_collisions.resolveMovement(dx, dy, dz, transform, body);

  // Fluid check
  body.isFluid = m_collisions.isFluidAt(
      transform.position.x,
      transform.position.y + body.aabbMin.y,
      transform.position.z);
  if (body.isFluid) {
    body.velocity.y = std::min(body.velocity.y, 0.0f);
  }
}

void PlayerControllerSystem::syncCameraFromPlayer() {
  int32_t idx = m_cachedPlayerIndex;
  if (idx < 0 ||
      !m_transforms.has(idx) || !m_players.has(idx)) {
    return;
  }

  const auto& transform = m_transforms.get(idx);
  const auto& player = m_players.get(idx);

  float cp = std::cos(player.pitch);
  m_camera.forward = glm::normalize(glm::vec3(
    cp * std::sin(player.yaw),
    std::sin(player.pitch),
    -cp * std::cos(player.yaw)
  ));
  m_camera.right = glm::normalize(glm::cross(m_camera.forward, m_camera.up));
  m_camera.position = transform.position + glm::vec3(0.0f, player.eyeHeight, 0.0f);
  m_cameraDirty = true;
}

} // namespace voxel
