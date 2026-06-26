#include <catch2/catch_test_macros.hpp>
#include "world/World.hpp"
#include "world/BlockRegistry.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "MockWorker.hpp"

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

  // Capture pool by raw pointer so the mock worker can fill in voxel data
  // to satisfy the chunk validation in World::onWorldGenDone().
  voxel::SharedPool* poolPtr = pool.get();
  voxel::TestChunkWorker worker(
    [&, poolPtr](int32_t slotIndex, int32_t, int32_t, uint32_t) {
      ++genCount;
      // Write bedrock at y=0 and some stone so the chunk passes validation.
      auto slot = poolPtr->view(slotIndex);
      constexpr int32_t sx = 16, sz = 16;
      for (int32_t z = 0; z < sz; ++z) {
        for (int32_t x = 0; x < sx; ++x) {
          slot.voxels[(0 * sz + z) * sx + x] = 7; // bedrock
          slot.voxels[(1 * sz + z) * sx + x] = 1; // some stone
        }
      }
    },
    [&](int32_t) { ++meshCount; }
  );
  // Null persistence — chunks go through generation path
  voxel::World world(*pool, blocks, cfg, worker, nullptr);

  // Initially centered at origin — not ready
  REQUIRE_FALSE(world.isReady());

  // Update should create chunks and trigger generation
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > 0);

  // Signal gen complete — should queue mesh.
  // Use a valid slot index: the pool allocates LIFO, so the last acquired slot
  // is the lowest index. Find the center chunk (0,0) and use its slot.
  int meshBefore = meshCount;
  const auto* centerChunk = world.getChunk(0, 0);
  REQUIRE(centerChunk != nullptr);
  int32_t centerSlot = centerChunk->slotIndex;
  world.onWorldGenDone(centerSlot);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(meshCount > meshBefore);
}

TEST_CASE("World restarts failed chunk meshes from scratch with a retry cap", "[world]") {
  auto cfg = makeTestConfig();
  auto pool = voxel::SharedPool::create(16, makeDims(cfg));
  voxel::BlockRegistry blocks(256);
  blocks.register_(voxel::BlockDefinition{.id = 1, .name = "stone"});

  int genCount = 0;
  int meshCount = 0;
  voxel::SharedPool* poolPtr = pool.get();
  voxel::TestChunkWorker worker(
    [&, poolPtr](int32_t slotIndex, int32_t, int32_t, uint32_t) {
      ++genCount;
      auto slot = poolPtr->view(slotIndex);
      constexpr int32_t sx = 16, sz = 16;
      for (int32_t z = 0; z < sz; ++z) {
        for (int32_t x = 0; x < sx; ++x) {
          slot.voxels[(0 * sz + z) * sx + x] = 7;
          slot.voxels[(1 * sz + z) * sx + x] = 1;
        }
      }
    },
    [&](int32_t) { ++meshCount; }
  );

  voxel::World world(*pool, blocks, cfg, worker, nullptr);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  const int genAfterInitialLoad = genCount;

  const auto* centerChunk = world.getChunk(0, 0);
  REQUIRE(centerChunk != nullptr);
  const int32_t centerSlot = centerChunk->slotIndex;

  world.onWorldGenDone(centerSlot);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  const int meshAfterFirstDispatch = meshCount;
  REQUIRE(meshAfterFirstDispatch > 0);

  world.onMeshDone(centerSlot, 0, 0, false);
  REQUIRE(world.getChunk(0, 0)->state == voxel::ChunkState::QueuedGen);
  REQUIRE(world.getChunk(0, 0)->meshRestartRetries == 1);

  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > genAfterInitialLoad);

  world.onWorldGenDone(centerSlot);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  const int meshAfterSecondDispatch = meshCount;
  REQUIRE(meshAfterSecondDispatch > meshAfterFirstDispatch);

  world.onMeshDone(centerSlot, 0, 0, false);
  REQUIRE(world.getChunk(0, 0)->state == voxel::ChunkState::QueuedGen);
  REQUIRE(world.getChunk(0, 0)->meshRestartRetries == 2);

  const int genAfterSecondRestart = genCount;
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > genAfterSecondRestart);

  world.onWorldGenDone(centerSlot);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  const int meshAfterThirdDispatch = meshCount;
  REQUIRE(meshAfterThirdDispatch > meshAfterSecondDispatch);

  world.onMeshDone(centerSlot, 0, 0, false);
  REQUIRE(world.getChunk(0, 0)->state == voxel::ChunkState::MeshFailed);
  REQUIRE(world.getChunk(0, 0)->meshRestartRetries == voxel::MAX_CHUNK_MESH_RESTART_RETRIES);

  const int genAfterFailureCap = genCount;
  const int meshAfterFailureCap = meshCount;
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount == genAfterFailureCap);
  REQUIRE(meshCount == meshAfterFailureCap);
}

TEST_CASE("World getBlockIdAt returns 0 for unloaded chunks", "[world]") {
  auto cfg = makeTestConfig();
  auto pool = voxel::SharedPool::create(16, makeDims(cfg));
  voxel::BlockRegistry blocks(256);

  voxel::TestChunkWorker worker;
  voxel::World world(*pool, blocks, cfg, worker, nullptr);

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

  voxel::TestChunkWorker worker;
  voxel::World world(*pool, blocks, cfg, worker, nullptr);

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
  REQUIRE(key == voxel::chunkKey(0, 0));
  REQUIRE(voxel::Chunk::makeKey(5, -3) == voxel::chunkKey(5, -3));
}
