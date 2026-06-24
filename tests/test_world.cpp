#include <catch2/catch_test_macros.hpp>
#include "world/World.hpp"
#include "world/BlockRegistry.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"

namespace {

auto makeTestConfig() -> voxel::GameConfig {
  voxel::GameConfig cfg{};
  cfg.chunkSize = 16;
  cfg.worldHeight = 256;
  cfg.renderDistance = 2; // small for testing
  cfg.worldSeed = 42;
  cfg.maxVertsPerChunk = 10000;
  cfg.maxIndicesPerChunk = 20000;
  cfg.vertexStrideFloats = 8;
  return cfg;
}

auto makeDims(const voxel::GameConfig& cfg) -> voxel::ChunkDimensions {
  return {
    cfg.chunkSize,
    cfg.worldHeight,
    cfg.chunkSize,
    cfg.maxVertsPerChunk,
    cfg.maxIndicesPerChunk,
    cfg.vertexStrideFloats,
  };
}

} // anonymous namespace

TEST_CASE("World basic chunk lifecycle", "[world]") {
  auto cfg = makeTestConfig();
  auto pool = voxel::SharedPool::create(16, makeDims(cfg));
  voxel::BlockRegistry blocks(256);
  blocks.register_(voxel::BlockDefinition{.id = 1, .name = "stone"});

  int genCount = 0;
  int meshCount = 0;

  voxel::World world(
    *pool, blocks, cfg,
    [&](int32_t, int32_t, int32_t, uint32_t) { ++genCount; },
    [&](int32_t) { ++meshCount; },
    {}, {} // no save callbacks
  );

  // Initially centered at origin — not ready
  REQUIRE_FALSE(world.isReady());

  // Update should create chunks and trigger generation
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > 0);

  // Signal gen complete — should queue mesh
  int meshBefore = meshCount;
  world.onWorldGenDone(0); // slot index 0
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(meshCount > meshBefore);
}

TEST_CASE("World getBlockIdAt returns 0 for unloaded chunks", "[world]") {
  auto cfg = makeTestConfig();
  auto pool = voxel::SharedPool::create(16, makeDims(cfg));
  voxel::BlockRegistry blocks(256);

  voxel::World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {}, {}, {});

  REQUIRE(world.getBlockIdAt(100, 64, 100) == 0);
}

TEST_CASE("World isSolid and isFluid", "[world]") {
  auto cfg = makeTestConfig();
  auto pool = voxel::SharedPool::create(16, makeDims(cfg));
  voxel::BlockRegistry blocks(256);

  // Register a solid block (id=1) and a liquid (id=2)
  voxel::BlockDefinition stone{.id = 1, .name = "stone"};
  stone.material.opaque = true;
  stone.material.liquid = false;
  blocks.register_(std::move(stone));

  voxel::BlockDefinition water{.id = 2, .name = "water"};
  water.material.opaque = false;
  water.material.transparent = true;
  water.material.liquid = true;
  water.collision = voxel::EMPTY_BLOCK_AABB;
  blocks.register_(std::move(water));

  voxel::World world(*pool, blocks, cfg,
    [](int32_t, int32_t, int32_t, uint32_t) {},
    [](int32_t) {}, {}, {});

  // Unloaded chunks — both return false
  REQUIRE_FALSE(world.isSolid(0, 64, 0));
  REQUIRE_FALSE(world.isFluid(0, 64, 0));

  // Below world — returns false
  REQUIRE_FALSE(world.isSolid(0, -1, 0));
  REQUIRE_FALSE(world.isSolid(0, 256, 0));
}

TEST_CASE("ChunkManager basic operations", "[world]") {
  voxel::ChunkManager cm;
  REQUIRE_FALSE(cm.has(0, 0));

  voxel::Chunk c{0, 0, 0};
  cm.set(c);
  REQUIRE(cm.has(0, 0));
  REQUIRE(cm.get(0, 0) != nullptr);
  REQUIRE(cm.get(0, 0)->chunkX == 0);

  cm.remove(0, 0);
  REQUIRE_FALSE(cm.has(0, 0));
}

TEST_CASE("ChunkState transitions", "[world]") {
  voxel::Chunk c{0, 0, 0};
  REQUIRE(c.state == voxel::ChunkState::QueuedGen);

  auto key = c.key();
  REQUIRE(key == "0:0");
  REQUIRE(voxel::Chunk::makeKey(5, -3) == "5:-3");
}
