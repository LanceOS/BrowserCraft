#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/core/InputState.hpp"
#include "engine/core/Config.hpp"
#include "engine/ecs/EntityManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/components/Components.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "world/World.hpp"
#include "world/BlockRegistry.hpp"
#include <glm/glm.hpp>
#include <cmath>

using namespace voxel;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static auto makeTestConfig() -> GameConfig {
  GameConfig cfg{};
  cfg.chunkSize = 16;
  cfg.worldHeight = 256;
  cfg.renderDistance = 2;
  cfg.worldSeed = 0;
  cfg.maxVertsPerChunk = 1000;
  cfg.maxIndicesPerChunk = 2000;
  cfg.vertexStrideFloats = 8;
  return cfg;
}

static auto makeDims(const GameConfig& cfg) -> ChunkDimensions {
  return {cfg.chunkSize, cfg.worldHeight, cfg.chunkSize,
          cfg.maxVertsPerChunk, cfg.maxIndicesPerChunk, cfg.vertexStrideFloats};
}

/// Register a minimal set of blocks for collision testing.
static void registerTestBlocks(BlockRegistry& blocks) {
  blocks.register_(BlockDefinition{
    .id = 1, .name = "stone",
    .collision = FULL_BLOCK_AABB,
  });
  blocks.register_(BlockDefinition{
    .id = 2, .name = "dirt",
    .collision = FULL_BLOCK_AABB,
  });
  blocks.register_(BlockDefinition{
    .id = 3, .name = "air",
    .collision = EMPTY_BLOCK_AABB, // air-like
  });
  blocks.register_(BlockDefinition{
    .id = 4, .name = "slab",
    .collision = {0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f}, // half slab
  });
  blocks.register_(BlockDefinition{
    .id = 5, .name = "water",
    .material = {.opaque = false, .transparent = true, .liquid = true},
    .collision = EMPTY_BLOCK_AABB, // no collision, but fluid
  });
}

/// Fill a chunk slot with a simple pattern: a flat stone floor at Y=groundY
/// with air above, and bedrock at Y=0.
static void fillFlatTerrain(ChunkSlot slot, int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                            int32_t groundY) {
  for (int32_t y = 0; y < sizeY; ++y) {
    for (int32_t z = 0; z < sizeZ; ++z) {
      for (int32_t x = 0; x < sizeX; ++x) {
        int32_t idx = (y * sizeZ + z) * sizeX + x;
        if (y == 0) {
          slot.voxels[idx] = 2; // bedrock (dirt)
        } else if (y < groundY) {
          slot.voxels[idx] = 1; // stone
        } else {
          slot.voxels[idx] = 0; // air
        }
      }
    }
  }
}

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
  REQUIRE(input.isPressed(InputState::KEY_W));  // pressed this frame
  REQUIRE(input.isHeld(InputState::KEY_W));     // >0 → held

  // clearFrameState demotes 1→2 (pressed→held)
  input.clearFrameState();
  REQUIRE_FALSE(input.isPressed(InputState::KEY_W)); // no longer newly pressed
  REQUIRE(input.isHeld(InputState::KEY_W));          // still held

  // Keep holding — setKey sees existing 2, stays 2 (held)
  input.setKey(InputState::KEY_W, true);
  REQUIRE_FALSE(input.isPressed(InputState::KEY_W)); // not newly pressed
  REQUIRE(input.isHeld(InputState::KEY_W));          // still held

  // Release
  input.setKey(InputState::KEY_W, false);
  REQUIRE_FALSE(input.isPressed(InputState::KEY_W));
  REQUIRE_FALSE(input.isHeld(InputState::KEY_W));
}

TEST_CASE("InputState rapid press and release", "[input]") {
  InputState input;

  // Press and release within a single frame
  input.setKey(InputState::KEY_A, true);
  REQUIRE(input.isPressed(InputState::KEY_A));
  input.setKey(InputState::KEY_A, false);
  // After release within same frame, isPressed returns false (state went 0→1→0)
  REQUIRE_FALSE(input.isPressed(InputState::KEY_A));
  REQUIRE_FALSE(input.isHeld(InputState::KEY_A));

  // clearFrameState on clean state is a no-op
  input.clearFrameState();
  REQUIRE_FALSE(input.isPressed(InputState::KEY_A));
}

TEST_CASE("InputState clearAll resets everything", "[input]") {
  InputState input;

  input.setKey(InputState::KEY_SPACE, true);
  input.addMouseDelta(10.0f, 5.0f);
  REQUIRE(input.isHeld(InputState::KEY_SPACE));
  REQUIRE(input.mouseDX() == Approx(10.0f));

  input.clearAll();
  REQUIRE_FALSE(input.isHeld(InputState::KEY_SPACE));
  REQUIRE(input.mouseDX() == Approx(0.0f));
  REQUIRE(input.mouseDY() == Approx(0.0f));
}

TEST_CASE("InputState mouse delta accumulate and clear", "[input]") {
  InputState input;

  input.addMouseDelta(3.0f, -1.0f);
  input.addMouseDelta(2.0f, 4.0f);
  REQUIRE(input.mouseDX() == Approx(5.0f));
  REQUIRE(input.mouseDY() == Approx(3.0f));

  input.clearFrameState();
  REQUIRE(input.mouseDX() == Approx(0.0f));
  REQUIRE(input.mouseDY() == Approx(0.0f));
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

  // Unknown key should be ignored — W and E remain unchanged
  input.clearFrameState(); // demote W/E from pressed to held
  input.setKeyByCode(GLFW_KEY_F1, true);
  CHECK(input.isHeld(InputState::KEY_W));   // still held (F1 didn't unpress it)
  CHECK(input.isHeld(InputState::KEY_E));   // still held
  CHECK(input.isHeld(InputState::KEY_SPACE)); // still held
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
// World collision functions
// ===========================================================================

TEST_CASE("World isSolid detects blocks", "[world][collision]") {
  auto cfg = makeTestConfig();
  auto pool = SharedPool::create(16, makeDims(cfg));
  BlockRegistry blocks(256);
  registerTestBlocks(blocks);

  World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {},
    {}, {});

  // Acquire a slot and fill it with a flat floor at Y=10 (with stone from Y=1..9)
  auto slot = pool->acquire();
  REQUIRE(slot.has_value());

  Chunk chunk{0, 0, slot->slotIndex};
  fillFlatTerrain(*slot, cfg.chunkSize, cfg.worldHeight, cfg.chunkSize, 10);

  // Manually insert the chunk into the world via ensureVisibleRadius...
  // Instead, let's update the world to trigger chunk creation, then write
  // into the created slot.

  // Update once to create chunks around (0,0)
  world.update(glm::vec3(8.0f, 40.0f, 8.0f));

  // The center chunk should exist. Find it and write our test data.
  auto* centerChunk = world.getChunk(0, 0);
  if (centerChunk) {
    auto cs = world.getChunkSlot(*centerChunk);
    fillFlatTerrain(cs, cfg.chunkSize, cfg.worldHeight, cfg.chunkSize, 10);
  }

  // Test isSolid at various positions within chunk (0,0)
  // Position (5, 0, 5) — bedrock (block id 2, has volume)
  // But only if the chunk exists and has voxels
  if (centerChunk) {
    // Y=0 should be solid (bedrock / dirt)
    CHECK(world.isSolid(5, 0, 5));
    // Y=5 should be solid (stone below groundY=10)
    CHECK(world.isSolid(5, 5, 5));
    // Y=9 should be solid (still below groundY=10)
    CHECK(world.isSolid(5, 9, 5));
    // Y=10 should be air (at ground level)
    CHECK_FALSE(world.isSolid(5, 10, 5));
    // Y=50 should be air (well above ground)
    CHECK_FALSE(world.isSolid(5, 50, 5));
    // Y out of bounds
    CHECK_FALSE(world.isSolid(5, -1, 5));
    CHECK_FALSE(world.isSolid(5, 300, 5));
  }
}

TEST_CASE("isSolidInChunk reads correct block data", "[world][collision]") {
  auto cfg = makeTestConfig();
  auto pool = SharedPool::create(16, makeDims(cfg));
  BlockRegistry blocks(256);
  registerTestBlocks(blocks);

  World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {},
    {}, {});

  world.update(glm::vec3(8.0f, 40.0f, 8.0f));
  auto* chunk = world.getChunk(0, 0);
  REQUIRE(chunk != nullptr);

  // Write known data into the chunk slot
  auto slot = world.getChunkSlot(*chunk);
  fillFlatTerrain(slot, cfg.chunkSize, cfg.worldHeight, cfg.chunkSize, 10);

  // Test isSolidInChunk
  CHECK(world.isSolidInChunk(0, 0, 0, *chunk));   // bedrock
  CHECK(world.isSolidInChunk(7, 5, 7, *chunk));   // stone
  CHECK_FALSE(world.isSolidInChunk(7, 10, 7, *chunk)); // air
  CHECK_FALSE(world.isSolidInChunk(7, 50, 7, *chunk)); // air

  // A slab block (ID 4) with partial collision at Y=5
  int32_t slabIdx = (5 * cfg.chunkSize + 7) * cfg.chunkSize + 3;
  slot.voxels[slabIdx] = 4;
  CHECK(world.isSolidInChunk(3, 5, 7, *chunk)); // slab has partial volume
}

// ===========================================================================
// Ground height scan logic
// ===========================================================================

TEST_CASE("groundHeightAt finds correct surface", "[world][spawn]") {
  auto cfg = makeTestConfig();
  auto pool = SharedPool::create(16, makeDims(cfg));
  BlockRegistry blocks(256);
  registerTestBlocks(blocks);

  World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {},
    {}, {});

  world.update(glm::vec3(8.0f, 40.0f, 8.0f));
  auto* chunk = world.getChunk(0, 0);
  REQUIRE(chunk != nullptr);

  auto slot = world.getChunkSlot(*chunk);
  // Fill with flat terrain: stone from Y=1..24, then air
  fillFlatTerrain(slot, cfg.chunkSize, cfg.worldHeight, cfg.chunkSize, 25);

  // Simulate groundHeightAt: scan from worldHeight-1 downward
  int32_t gx = 7, gz = 7;
  int32_t foundY = -1;
  for (int32_t y = cfg.worldHeight - 1; y >= 0; --y) {
    if (world.isSolid(gx, y, gz)) { foundY = y; break; }
  }
  REQUIRE(foundY == 24);

  // Should also find Y=0 (bedrock) if no other blocks exist
  gx = 0, gz = 0;
  foundY = -1;
  for (int32_t y = cfg.worldHeight - 1; y >= 0; --y) {
    if (world.isSolid(gx, y, gz)) { foundY = y; break; }
  }
  REQUIRE(foundY == 24); // same flat terrain covers the whole chunk
}

TEST_CASE("3x3 grid scan avoids cave holes", "[world][spawn]") {
  auto cfg = makeTestConfig();
  auto pool = SharedPool::create(16, makeDims(cfg));
  BlockRegistry blocks(256);
  registerTestBlocks(blocks);

  World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {},
    {}, {});

  world.update(glm::vec3(8.0f, 40.0f, 8.0f));
  auto* chunk = world.getChunk(0, 0);
  REQUIRE(chunk != nullptr);

  auto slot = world.getChunkSlot(*chunk);
  // Create an uneven surface: ground at 30 most places, but a "cave shaft"
  // at column (7,7) where ground is only at Y=5
  fillFlatTerrain(slot, cfg.chunkSize, cfg.worldHeight, cfg.chunkSize, 30);

  // Dig a cave shaft at (7, z=7): remove blocks from Y=5..29
  for (int32_t y = 5; y < 30; ++y) {
    int32_t idx = (y * cfg.chunkSize + 7) * cfg.chunkSize + 7;
    slot.voxels[idx] = 0;
  }

  // Single-column scan at (7,7) — finds Y=4 (the cave floor)
  int32_t singleY = -1;
  for (int32_t y = cfg.worldHeight - 1; y >= 0; --y) {
    if (world.isSolid(7, y, 7)) { singleY = y; break; }
  }
  REQUIRE(singleY == 4); // cave floor, not the surface

  // 3x3 grid scan around (7,7) — finds highest nearby ground (Y=29)
  int32_t highestY = -1;
  for (int32_t dz = -1; dz <= 1; ++dz) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      int32_t sx = 7 + dx;
      int32_t sz = 7 + dz;
      for (int32_t y = cfg.worldHeight - 1; y >= 0; --y) {
        if (world.isSolid(sx, y, sz)) {
          if (y > highestY) highestY = y;
          break;
        }
      }
    }
  }
  // The highest nearby column without a cave shaft is at (8,7), (6,7), (7,6), or (7,8)
  // which have ground at Y=29
  REQUIRE(highestY == 29);
}

// ===========================================================================
// Player push-out-of-blocks safety
// ===========================================================================

TEST_CASE("Player collides when inside terrain", "[world][collision]") {
  auto cfg = makeTestConfig();
  auto pool = SharedPool::create(16, makeDims(cfg));
  BlockRegistry blocks(256);
  registerTestBlocks(blocks);

  World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {},
    {}, {});

  world.update(glm::vec3(8.0f, 40.0f, 8.0f));
  auto* chunk = world.getChunk(0, 0);
  REQUIRE(chunk != nullptr);

  auto slot = world.getChunkSlot(*chunk);
  fillFlatTerrain(slot, cfg.chunkSize, cfg.worldHeight, cfg.chunkSize, 64);
  REQUIRE(chunk != nullptr);

  cmp::RigidBody body;
  // Player AABB: [-0.3, 0, -0.3] to [0.3, 1.8, 0.3]

  // Player at Y=64 should be on the surface (ground is at Y=63)
  glm::vec3 onSurface(0.0f, 64.0f, 0.0f);
  // Check collision using the same logic as collidesAt
  auto checkCollision = [&](const glm::vec3& pos) -> bool {
    glm::vec3 minP = pos + body.aabbMin;
    glm::vec3 maxP = pos + body.aabbMax;
    int32_t minX = static_cast<int32_t>(std::floor(minP.x));
    int32_t maxX = static_cast<int32_t>(std::floor(maxP.x));
    int32_t minY = static_cast<int32_t>(std::floor(minP.y));
    int32_t maxY = static_cast<int32_t>(std::floor(maxP.y));
    int32_t minZ = static_cast<int32_t>(std::floor(minP.z));
    int32_t maxZ = static_cast<int32_t>(std::floor(maxP.z));
    for (int32_t y = minY; y <= maxY; ++y) {
      if (y < 0 || y >= cfg.worldHeight) continue;
      for (int32_t z = minZ; z <= maxZ; ++z) {
        for (int32_t x = minX; x <= maxX; ++x) {
          if (world.isSolid(x, y, z)) return true;
        }
      }
    }
    return false;
  };

  // On surface — should not collide (feet at Y=64, ground blocks at Y=63)
  CHECK_FALSE(checkCollision(onSurface));

  // Inside terrain at Y=10 — should collide (blocks at Y=1..63)
  glm::vec3 insideTerrain(0.0f, 10.0f, 0.0f);
  CHECK(checkCollision(insideTerrain));

  // At Y=65 — should not collide (one block above ground)
  glm::vec3 aboveSurface(0.0f, 65.0f, 0.0f);
  CHECK_FALSE(checkCollision(aboveSurface));
}

// ===========================================================================
// Block registry collision definitions
// ===========================================================================

TEST_CASE("BlockAABB hasVolume checks correctly", "[block]") {
  CHECK(FULL_BLOCK_AABB.hasVolume());
  CHECK_FALSE(EMPTY_BLOCK_AABB.hasVolume());

  BlockAABB slab{0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f};
  CHECK(slab.hasVolume());

  BlockAABB zero{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  CHECK_FALSE(zero.hasVolume());
}

TEST_CASE("Blocks with EMPTY_BLOCK_AABB are not solid", "[block]") {
  BlockRegistry reg(256);
  reg.register_(BlockDefinition{.id = 1, .name = "air", .collision = EMPTY_BLOCK_AABB});
  reg.register_(BlockDefinition{.id = 2, .name = "stone", .collision = FULL_BLOCK_AABB});

  auto* air = reg.tryGet(1);
  REQUIRE(air != nullptr);
  CHECK_FALSE(air->collision.hasVolume());

  auto* stone = reg.tryGet(2);
  REQUIRE(stone != nullptr);
  CHECK(stone->collision.hasVolume());
}

TEST_CASE("Fluid blocks are not solid", "[block]") {
  BlockRegistry reg(256);
  reg.register_(BlockDefinition{
    .id = 1, .name = "water",
    .material = {.opaque = false, .transparent = true, .liquid = true},
    .collision = EMPTY_BLOCK_AABB,
  });

  auto* water = reg.tryGet(1);
  REQUIRE(water != nullptr);
  CHECK(water->material.liquid);
  CHECK_FALSE(water->collision.hasVolume()); // can walk through
}

// ===========================================================================
// World coordinate utilities
// ===========================================================================

TEST_CASE("worldToChunk and mod helpers", "[world]") {
  // These are inline functions in World.hpp
  // worldToChunk(coord, size) = floor(coord / size)
  // mod(value, size) = positive modulus

  // Standard chunk size = 16
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

  // Positive modulo
  CHECK(mod(0, CS) == 0);
  CHECK(mod(15, CS) == 15);
  CHECK(mod(16, CS) == 0);
  CHECK(mod(-1, CS) == 15); // wraps around
  CHECK(mod(-16, CS) == 0);
}

// ===========================================================================
// Chunk seed determinism
// ===========================================================================

TEST_CASE("chunkSeed produces deterministic results", "[world]") {
  uint32_t seed = 12345;
  uint32_t s1 = chunkSeed(0, 0, seed);
  uint32_t s2 = chunkSeed(0, 0, seed);
  CHECK(s1 == s2); // same input → same output

  uint32_t s3 = chunkSeed(1, 0, seed);
  CHECK(s1 != s3); // different coords → different seed

  uint32_t s4 = chunkSeed(0, 1, seed);
  CHECK(s1 != s4);
  CHECK(s3 != s4);
}
