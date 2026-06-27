#include "PlayerControllerSystem.hpp"
#include "engine/ecs/EntityManager.hpp"
#include "game/WorldController.hpp"
#include "engine/save/SaveManager.hpp"
#include "world/World.hpp"
#include "world/terrain/TerrainRaycast.hpp"
#include "world/terrain/TerrainBrush.hpp"
#include "world/terrain/TerrainEditAPI.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

void syncHotbarSelection(const terrain::InputState& input, terrain::cmp::Player& player) {
  if (input.isPressed(terrain::InputState::KEY_1)) player.selectedHotbarSlot = 0;
  else if (input.isPressed(terrain::InputState::KEY_2)) player.selectedHotbarSlot = 1;
  else if (input.isPressed(terrain::InputState::KEY_3)) player.selectedHotbarSlot = 2;
  else if (input.isPressed(terrain::InputState::KEY_4)) player.selectedHotbarSlot = 3;
  else if (input.isPressed(terrain::InputState::KEY_5)) player.selectedHotbarSlot = 4;
  else if (input.isPressed(terrain::InputState::KEY_6)) player.selectedHotbarSlot = 5;
  else if (input.isPressed(terrain::InputState::KEY_7)) player.selectedHotbarSlot = 6;
  else if (input.isPressed(terrain::InputState::KEY_8)) player.selectedHotbarSlot = 7;
  else if (input.isPressed(terrain::InputState::KEY_9)) player.selectedHotbarSlot = 8;
}

auto highestGroundInFootprint(
    const terrain::EntityCollisions& collisions,
    const terrain::cmp::RigidBody& body,
    const glm::vec3& position,
    int32_t startY) -> int32_t
{
  const int32_t minGX = static_cast<int32_t>(std::floor(position.x + body.aabbMin.x));
  const int32_t maxGX = static_cast<int32_t>(std::floor(position.x + body.aabbMax.x));
  const int32_t minGZ = static_cast<int32_t>(std::floor(position.z + body.aabbMin.z));
  const int32_t maxGZ = static_cast<int32_t>(std::floor(position.z + body.aabbMax.z));
  int32_t highest = -1;

  for (int32_t gz = minGZ; gz <= maxGZ; ++gz) {
    for (int32_t gx = minGX; gx <= maxGX; ++gx) {
      const int32_t groundY = collisions.groundHeightAt(
          static_cast<float>(gx) + 0.5f,
          static_cast<float>(gz) + 0.5f,
          startY);
      if (groundY > highest) highest = groundY;
    }
  }

  return highest;
}

auto lookDirectionFromPlayer(const terrain::cmp::Player& player) -> glm::vec3 {
  const float cosPitch = std::cos(player.pitch);
  return glm::normalize(glm::vec3(
      std::sin(player.yaw) * cosPitch,
      std::sin(player.pitch),
      -std::cos(player.yaw) * cosPitch
  ));
}

} // namespace

namespace terrain {

PlayerControllerSystem::PlayerControllerSystem(
    GLFWwindow* window,
    InputState& input,
    ComponentStore<cmp::Transform>& transforms,
    ComponentStore<cmp::RigidBody>& bodies,
    ComponentStore<cmp::Player>& players,
    WorldController& worldController,
    WorldGenPipeline& pipeline,
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
  , m_worldController(worldController)
  , m_world(worldController.world())
  , m_pipeline(pipeline)
  , m_camera(camera)
  , m_ui(ui)
  , m_session(session)
  , m_cameraDirty(cameraDirty)
  , m_collisions(worldController.world(), config)
{}

auto PlayerControllerSystem::name() const -> const std::string& {
  static const std::string kName = "PlayerController";
  return kName;
}

auto PlayerControllerSystem::stage() const -> SystemStage {
  return SystemStage::Physics;
}

void PlayerControllerSystem::update(TickContext& ctx) {
  if (m_session.state() != GameState::InGame &&
      m_session.state() != GameState::GeneratingWorld) {
    return;
  }

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

  m_prevPosition = transform.position;

  syncHotbarSelection(m_input, player);
  const bool canControl = (m_session.state() == GameState::InGame ||
                          m_session.state() == GameState::GeneratingWorld) &&
    (m_ui.state() == UIState::InGame) &&
    !m_ui.isInventoryOpen();

  if (!canControl) {
    body.velocity.x = 0.0f;
    body.velocity.z = 0.0f;
    return;
  }

  if (glfwGetInputMode(m_window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  }
  m_input.pointerLocked = true;

  applyMouseLook(ctx.dt);

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
    float speed = m_input.isHeld(InputState::KEY_SHIFT) ? player.sprintSpeed
                                                        : player.walkSpeed;
    transform.position += moveDir * speed * ctx.dt;
    body.velocity = glm::vec3(0.0f);
    return;
  }

  applyMovement(ctx.dt, transform, body, player, moveDir);
  handleTerrainInteraction(transform, body, player);
}

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

  float speed = m_input.isHeld(InputState::KEY_SHIFT) ? player.sprintSpeed
                                                      : player.walkSpeed;

  int32_t gY = highestGroundInFootprint(
      m_collisions,
      body,
      transform.position,
      std::max(0, static_cast<int32_t>(
          std::floor(transform.position.y + body.aabbMin.y))));
  bool grounded = body.onGround != 0;
  if (gY >= 0) {
    float feetY = transform.position.y + body.aabbMin.y;
    float surfY = static_cast<float>(gY + 1);
    grounded = grounded || (feetY - surfY) <= 0.55f;
  }

  bool spaceHeld = m_input.isHeld(InputState::KEY_SPACE);
  bool justLanded = grounded && !m_prevOnGround;
  if (spaceHeld && (grounded || body.isFluid) &&
      (m_input.isPressed(InputState::KEY_SPACE) || justLanded)) {
    body.velocity.y = body.isFluid ? kSwimSpeed : kJumpSpeed;
    body.onGround = 0;
    body.isFluid = 0;
  }
  m_prevOnGround = grounded;

  body.velocity.y -= body.gravity * dt;

  body.velocity.x *= body.drag;
  body.velocity.z *= body.drag;

  body.velocity.x = 0.0f;
  body.velocity.z = 0.0f;

  float dx = moveDir.x * speed * dt;
  float dz = moveDir.z * speed * dt;
  float dy = body.velocity.y * dt;

  m_collisions.resolveMovement(dx, dy, dz, transform, body);

  body.isFluid = m_collisions.isFluidAt(
      transform.position.x,
      transform.position.y + body.aabbMin.y,
      transform.position.z);
  if (body.isFluid) {
    body.velocity.y = std::min(body.velocity.y, 0.0f);
  }
}

void PlayerControllerSystem::handleTerrainInteraction(
    const cmp::Transform& transform,
    const cmp::RigidBody& /*body*/,
    const cmp::Player& player)
{
  if (!m_input.pointerLocked) return;
  if (!m_input.isMousePressed(GLFW_MOUSE_BUTTON_LEFT) &&
      !m_input.isMousePressed(GLFW_MOUSE_BUTTON_RIGHT)) {
    return;
  }

  const glm::vec3 origin = transform.position + glm::vec3(0.0f, player.eyeHeight, 0.0f);
  const glm::vec3 lookDir = lookDirectionFromPlayer(player);
  const TerrainRaycastHit hit = raycastTerrain(m_world, origin, lookDir, kReachDistance);
  if (!hit.hit) return;

  // Defaults for terrain brush
  float radius = 3.0f;
  float strength = 1.2f;

  TerrainBrush brush{};
  brush.center = hit.position;
  brush.radius = radius;
  brush.strength = strength;
  brush.planeNormal = hit.normal;

  auto* sm = m_worldController.saveManager();

  if (m_input.isMousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
    // Left Click: Carve terrain (Subtract Sphere)
    brush.type = BrushType::SubtractSphere;
    if (sm) sm->recordTerrainEdit(brush);
    TerrainEditAPI::applyBrush(m_world, m_pipeline, brush);
  } else if (m_input.isMousePressed(GLFW_MOUSE_BUTTON_RIGHT)) {
    // Right Click: Build terrain (Add Sphere)
    brush.type = BrushType::AddSphere;
    if (sm) sm->recordTerrainEdit(brush);
    TerrainEditAPI::applyBrush(m_world, m_pipeline, brush);
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

  m_camera.forward = lookDirectionFromPlayer(player);
  m_camera.right = glm::normalize(glm::cross(m_camera.forward, m_camera.up));
  m_camera.position = transform.position + glm::vec3(0.0f, player.eyeHeight, 0.0f);
  m_cameraDirty = true;
}

void PlayerControllerSystem::interpolateCamera(float alpha) {
  int32_t idx = m_cachedPlayerIndex;
  if (idx < 0 || !m_transforms.has(idx) || !m_players.has(idx)) {
    return;
  }

  const auto& transform = m_transforms.get(idx);
  const auto& player = m_players.get(idx);

  glm::vec3 interpPos = glm::mix(m_prevPosition, transform.position, alpha);
  m_camera.position = interpPos + glm::vec3(0.0f, player.eyeHeight, 0.0f);
  m_camera.forward = lookDirectionFromPlayer(player);
  m_camera.right = glm::normalize(glm::cross(m_camera.forward, m_camera.up));
  m_cameraDirty = true;
}

} // namespace terrain
