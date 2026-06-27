#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/core/InputState.hpp"
#include "engine/core/Config.hpp"
#include "engine/ecs/EntityManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/components/Components.hpp"
#include "world/ChunkCoords.hpp"
#include "world/Chunk.hpp"
#include <glm/glm.hpp>
#include <cmath>

using namespace terrain;
using Catch::Approx;

// ===========================================================================
// InputState lifecycle
// ===========================================================================

TEST_CASE("InputState key press and hold", "[input]") {
  InputState input;

  // Initially all keys are up
  REQUIRE_FALSE(input.isPressed(InputState::KEY_W));
  REQUIRE_FALSE(input.isHeld(InputState::KEY_W));

  // Press W
  input.setKey(InputState::KEY_W, true);
  REQUIRE(input.isPressed(InputState::KEY_W));
  REQUIRE(input.isHeld(InputState::KEY_W));

  // Next frame: W is still held, but no longer "pressed" (meaning transition this frame)
  input.clearFrameState();
  REQUIRE_FALSE(input.isPressed(InputState::KEY_W));
  REQUIRE(input.isHeld(InputState::KEY_W));

  // Release W
  input.setKey(InputState::KEY_W, false);
  REQUIRE_FALSE(input.isPressed(InputState::KEY_W));
  REQUIRE_FALSE(input.isHeld(InputState::KEY_W));
}

TEST_CASE("InputState rapid press and release", "[input]") {
  InputState input;

  // Press E
  input.setKey(InputState::KEY_E, true);
  REQUIRE(input.isPressed(InputState::KEY_E));

  // Release E
  input.setKey(InputState::KEY_E, false);
  REQUIRE_FALSE(input.isPressed(InputState::KEY_E));
  REQUIRE_FALSE(input.isHeld(InputState::KEY_E));
}

TEST_CASE("InputState clearAll resets everything", "[input]") {
  InputState input;

  input.setKey(InputState::KEY_W, true);
  input.setMouseButton(GLFW_MOUSE_BUTTON_LEFT, true);
  input.addMouseDelta(10.0f, -5.0f);

  input.clearAll();

  CHECK_FALSE(input.isHeld(InputState::KEY_W));
  CHECK_FALSE(input.isMouseHeld(GLFW_MOUSE_BUTTON_LEFT));
  CHECK(input.mouseDX() == Approx(0.0f));
  CHECK(input.mouseDY() == Approx(0.0f));
}

TEST_CASE("InputState mouse delta accumulate and clear", "[input]") {
  InputState input;

  input.addMouseDelta(5.0f, 10.0f);
  input.addMouseDelta(-2.0f, 3.0f);

  CHECK(input.mouseDX() == Approx(3.0f));
  CHECK(input.mouseDY() == Approx(13.0f));

  input.clearFrameState();
  CHECK(input.mouseDX() == Approx(0.0f));
  CHECK(input.mouseDY() == Approx(0.0f));
}

TEST_CASE("InputState mouse buttons distinguish pressed from held", "[input]") {
  InputState input;

  input.setMouseButton(GLFW_MOUSE_BUTTON_LEFT, true);
  REQUIRE(input.isMousePressed(GLFW_MOUSE_BUTTON_LEFT));
  REQUIRE(input.isMouseHeld(GLFW_MOUSE_BUTTON_LEFT));

  input.clearFrameState();
  REQUIRE_FALSE(input.isMousePressed(GLFW_MOUSE_BUTTON_LEFT));
  REQUIRE(input.isMouseHeld(GLFW_MOUSE_BUTTON_LEFT));
  REQUIRE(input.mouseButton(GLFW_MOUSE_BUTTON_LEFT) == 2);

  input.setMouseButton(GLFW_MOUSE_BUTTON_LEFT, false);
  REQUIRE_FALSE(input.isMousePressed(GLFW_MOUSE_BUTTON_LEFT));
  REQUIRE_FALSE(input.isMouseHeld(GLFW_MOUSE_BUTTON_LEFT));
}

TEST_CASE("InputState setKeyByCode maps GLFW keys correctly", "[input]") {
  InputState input;

  // Simulate what the GLFW callback does
  input.setKeyByCode(GLFW_KEY_W, true);
  REQUIRE(input.isHeld(InputState::KEY_W));

  input.setKeyByCode(GLFW_KEY_E, true);
  REQUIRE(input.isHeld(InputState::KEY_E));

  input.setKeyByCode(GLFW_KEY_SPACE, true);
  REQUIRE(input.isHeld(InputState::KEY_SPACE));

  input.setKeyByCode(GLFW_KEY_RIGHT_CONTROL, true);
  REQUIRE(input.isHeld(InputState::KEY_CTRL));

  // Unknown key should be ignored
  input.clearFrameState();
  input.setKeyByCode(GLFW_KEY_F1, true);
  CHECK(input.isHeld(InputState::KEY_W));
  CHECK(input.isHeld(InputState::KEY_E));
  CHECK(input.isHeld(InputState::KEY_SPACE));
  CHECK(input.isHeld(InputState::KEY_CTRL));
}

// ===========================================================================
// EntityManager and component store integration
// ===========================================================================

TEST_CASE("Player entity creation and component access", "[entity]") {
  EntityManager em(64);
  ComponentStore<cmp::Transform> transforms(64);
  ComponentStore<cmp::RigidBody> bodies(64);
  ComponentStore<cmp::Player> players(64);

  // Create a player entity (mirrors Game::createPlayer)
  int32_t entityId = em.allocate();
  int32_t idx = EntityManager::indexOf(entityId);
  transforms.add(idx);
  bodies.add(idx);
  players.add(idx);

  REQUIRE(em.isAlive(entityId));
  REQUIRE(transforms.has(idx));
  REQUIRE(bodies.has(idx));
  REQUIRE(players.has(idx));

  // Modify components
  auto& t = transforms.get(idx);
  t.position = glm::vec3(10.0f, 20.0f, 30.0f);
  REQUIRE(t.position.y == Approx(20.0f));

  auto& b = bodies.get(idx);
  b.velocity = glm::vec3(1.0f, 2.0f, 3.0f);
  b.onGround = 1;
  REQUIRE(b.onGround == 1);

  auto& p = players.get(idx);
  p.yaw = 1.5f;
  p.pitch = 0.5f;
  REQUIRE(p.yaw == Approx(1.5f));
}

// ===========================================================================
// World coordinate utilities
// ===========================================================================

TEST_CASE("chunk coordinate helpers", "[world]") {
  constexpr int32_t CS = 16;

  // Positive coordinates
  CHECK(worldToChunk(0.0f, CS) == 0);
  CHECK(worldToChunk(15.9f, CS) == 0);
  CHECK(worldToChunk(16.0f, CS) == 1);
  CHECK(worldToChunk(31.9f, CS) == 1);

  // Negative coordinates
  CHECK(worldToChunk(-0.1f, CS) == -1);
  CHECK(worldToChunk(-16.0f, CS) == -1);
  CHECK(worldToChunk(-16.1f, CS) == -2);

  CHECK(floorToChunk(0, CS) == 0);
  CHECK(floorToChunk(15, CS) == 0);
  CHECK(floorToChunk(16, CS) == 1);
  CHECK(floorToChunk(-1, CS) == -1);
  CHECK(floorToChunk(-16, CS) == -1);
  CHECK(floorToChunk(-17, CS) == -2);

  // Positive modulo
  CHECK(mod(0, CS) == 0);
  CHECK(mod(15, CS) == 15);
  CHECK(mod(16, CS) == 0);
  CHECK(mod(-1, CS) == 15);
  CHECK(mod(-16, CS) == 0);
}

// ===========================================================================
// Chunk seed determinism
// ===========================================================================

TEST_CASE("chunkSeed produces deterministic results", "[world]") {
  uint32_t seed = 12345;
  uint32_t s1 = chunkSeed(0, 0, seed);
  uint32_t s2 = chunkSeed(0, 0, seed);
  CHECK(s1 == s2);

  uint32_t s3 = chunkSeed(1, 0, seed);
  CHECK(s1 != s3);

  uint32_t s4 = chunkSeed(0, 1, seed);
  CHECK(s1 != s4);
  CHECK(s3 != s4);
}
