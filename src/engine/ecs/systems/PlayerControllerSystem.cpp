#include "PlayerControllerSystem.hpp"
#include "game/Game.hpp"
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
  , m_world(world)
  , m_camera(camera)
  , m_config(config)
  , m_ui(ui)
  , m_session(session)
  , m_cameraDirty(cameraDirty)
{}

auto PlayerControllerSystem::name() const -> const std::string& {
  static const std::string kName = "PlayerController";
  return kName;
}

auto PlayerControllerSystem::stage() const -> SystemStage {
  return SystemStage::Physics;
}

void PlayerControllerSystem::update(Game& state, float dt) {
  // Only run controls while actively playing
  if (m_session.state() != GameState::InGame &&
      m_session.state() != GameState::GeneratingWorld) {
    return;
  }

  // Resolve player entity index from the current Game state (always fresh,
  // survives respawns and entity re-allocation).
  int32_t playerId = state.playerEntityId();
  if (playerId == 0) return;
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

  applyMouseLook(dt);

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

  if (!m_world.hasTerrain()) {
    // No terrain yet — allow mouse look and horizontal movement but no gravity
    float speed = m_input.isHeld(InputState::KEY_SHIFT) ? player.sprintSpeed
                                                        : player.walkSpeed;
    transform.position += moveDir * speed * dt;
    body.velocity = glm::vec3(0.0f);
    syncCameraFromPlayer();
    return;
  }

  applyMovement(dt, transform, body, player, moveDir);
  syncCameraFromPlayer();
}

void PlayerControllerSystem::pushPlayerOutOfBlocks(int32_t entityIndex) {
  if (entityIndex < 0 || !m_bodies.has(entityIndex) || !m_transforms.has(entityIndex)) return;
  auto& transform = m_transforms.get(entityIndex);
  auto& body = m_bodies.get(entityIndex);
  int32_t maxIter = 256; // safety limit
  while (collidesAt(transform.position, body) && --maxIter > 0) {
    transform.position.y += 0.05f;
  }
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

  if (m_input.isPressed(InputState::KEY_SPACE) && (body.onGround || body.isFluid)) {
    body.velocity.y = body.isFluid ? kSwimSpeed : kJumpSpeed;
    body.onGround = 0;
    body.isFluid = 0;
  }

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

  // Sub-step if any axis moves more than 0.5 blocks per step (tunneling prevention)
  float maxDelta = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
  int32_t steps = std::max(1, static_cast<int32_t>(std::ceil(maxDelta / 0.5f)));
  float invSteps = 1.0f / static_cast<float>(steps);

  static constexpr float kStepHeight = 0.52f;

  for (int32_t s = 0; s < steps; ++s) {
    float subDx = dx * invSteps;
    float subDy = dy * invSteps;
    float subDz = dz * invSteps;

    glm::vec3 stepPos = transform.position;

    // --- Y axis (gravity + collision) ---
    glm::vec3 yStep = stepPos + glm::vec3(0.0f, subDy, 0.0f);
    if (!collidesAt(yStep, body)) {
      stepPos = yStep;
      body.onGround = 0;
    } else if (subDy <= 0.0f) {
      // Landed on ground — snap to surface
      int32_t groundY = groundHeightAt(
          stepPos.x, stepPos.z,
          std::max(0, static_cast<int32_t>(std::floor(stepPos.y + body.aabbMin.y))));
      if (groundY >= 0) {
        stepPos.y = static_cast<float>(groundY + 1);
        body.onGround = 1;
        // Safety push-up if still colliding
        int32_t safetyIters = 64;
        while (collidesAt(stepPos, body) && --safetyIters > 0) {
          stepPos.y += 0.05f;
        }
      } else {
        body.onGround = 0;
      }
      body.velocity.y = 0.0f;
      subDy = 0.0f; // Ground absorbs remaining fall
    } else {
      // Hit ceiling
      body.velocity.y = 0.0f;
      subDy = 0.0f;
    }

    // --- X axis (with step-assist) ---
    glm::vec3 xStep = stepPos + glm::vec3(subDx, 0.0f, 0.0f);
    if (!collidesAt(xStep, body)) {
      stepPos.x = xStep.x;
    } else {
      // Step-assist: if on ground, try raising the player to step onto slabs/stairs
      if (body.onGround && subDx != 0.0f) {
        glm::vec3 raised = stepPos + glm::vec3(subDx, kStepHeight, 0.0f);
        if (!collidesAt(raised, body)) {
          stepPos = raised;
        } else {
          body.velocity.x = 0.0f;
        }
      } else {
        body.velocity.x = 0.0f;
      }
    }

    // --- Z axis (with step-assist) ---
    glm::vec3 zStep = stepPos + glm::vec3(0.0f, 0.0f, subDz);
    if (!collidesAt(zStep, body)) {
      stepPos.z = zStep.z;
    } else {
      if (body.onGround && subDz != 0.0f) {
        glm::vec3 raised = stepPos + glm::vec3(0.0f, kStepHeight, subDz);
        if (!collidesAt(raised, body)) {
          stepPos = raised;
        } else {
          body.velocity.z = 0.0f;
        }
      } else {
        body.velocity.z = 0.0f;
      }
    }

    transform.position = stepPos;
  }

  // Fluid check
  int32_t sampleY = std::max(0, static_cast<int32_t>(
      std::floor(transform.position.y + body.aabbMin.y)));
  int32_t sampleX = static_cast<int32_t>(std::floor(transform.position.x));
  int32_t sampleZ = static_cast<int32_t>(std::floor(transform.position.z));
  body.isFluid = m_world.isFluid(sampleX, sampleY, sampleZ);
  if (body.isFluid) {
    body.velocity.y = std::min(body.velocity.y, 0.0f);
  }
}

auto PlayerControllerSystem::collidesAt(
    const glm::vec3& candidatePosition,
    const cmp::RigidBody& body) const -> bool
{
  const glm::vec3 minPoint = candidatePosition + body.aabbMin;
  const glm::vec3 maxPoint = candidatePosition + body.aabbMax;

  int32_t minX = static_cast<int32_t>(std::floor(minPoint.x));
  int32_t maxX = static_cast<int32_t>(std::floor(maxPoint.x));
  int32_t minY = static_cast<int32_t>(std::floor(minPoint.y));
  int32_t maxY = static_cast<int32_t>(std::floor(maxPoint.y));
  int32_t minZ = static_cast<int32_t>(std::floor(minPoint.z));
  int32_t maxZ = static_cast<int32_t>(std::floor(maxPoint.z));

  const int32_t chunkSize = m_config.chunkSize;
  const int32_t worldHeight = m_config.worldHeight;

  // Cache the last-resolved chunk to avoid repeated map lookups.
  constexpr int32_t kSentinel = int32_t(0x7FFFFFFF);
  const Chunk* cachedChunk = nullptr;
  int32_t cachedCX = kSentinel;
  int32_t cachedCZ = kSentinel;

  for (int32_t y = minY; y <= maxY; ++y) {
    if (y < 0 || y >= worldHeight) continue;
    for (int32_t z = minZ; z <= maxZ; ++z) {
      int32_t cz = worldToChunk(static_cast<float>(z), chunkSize);
      for (int32_t x = minX; x <= maxX; ++x) {
        int32_t cx = worldToChunk(static_cast<float>(x), chunkSize);

        if (cx != cachedCX || cz != cachedCZ) {
          cachedChunk = m_world.getChunk(cx, cz);
          cachedCX = cx;
          cachedCZ = cz;
        }
        // Treat unloaded chunks as solid to prevent walking through
        // world edges or falling through ungenerated terrain.
        if (!cachedChunk) return true;

        if (m_world.isSolidInChunk(x, y, z, *cachedChunk)) return true;
      }
    }
  }
  return false;
}

auto PlayerControllerSystem::groundHeightAt(
    float worldX, float worldZ, int32_t startY) const -> int32_t
{
  int32_t x = static_cast<int32_t>(std::floor(worldX));
  int32_t z = static_cast<int32_t>(std::floor(worldZ));
  int32_t y = std::clamp(startY, 0, m_config.worldHeight - 1);

  for (; y >= 0; --y) {
    if (m_world.isSolid(x, y, z)) return y;
  }
  return -1;
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
