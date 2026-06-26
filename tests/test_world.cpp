#include <catch2/catch_test_macros.hpp>
#include "world/World.hpp"
#include "world/ChunkCoords.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "MockWorker.hpp"
#include <utility>
#include <vector>

namespace {

auto makeTestConfig() -> terrain::GameConfig {
  terrain::GameConfig cfg{};
  cfg.chunkSize = 16;
  cfg.worldHeight = 256;
  cfg.renderDistance = 2; // small for testing
  cfg.worldSeed = 42;
  cfg.maxVertsPerChunk = 10000;
  cfg.maxIndicesPerChunk = 20000;
  cfg.vertexStrideFloats = 8;
  return cfg;
}

auto makeDims(const terrain::GameConfig& cfg) -> terrain::ChunkDimensions {
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
  auto pool = terrain::SharedPool::create(16, makeDims(cfg));

  int genCount = 0;
  int meshCount = 0;

  terrain::SharedPool* poolPtr = pool.get();
  terrain::TestChunkWorker worker(
    [&](int32_t, int32_t, int32_t, uint32_t) {
      ++genCount;
    },
    [&](int32_t) { ++meshCount; }
  );
  // Null persistence — chunks go through generation path
  terrain::World world(*pool, cfg, worker, nullptr);

  // Initially centered at origin — not ready
  REQUIRE_FALSE(world.isReady());

  // Update should create chunks and trigger generation
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > 0);

  // Signal gen complete — should queue mesh.
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
  auto pool = terrain::SharedPool::create(16, makeDims(cfg));

  int genCount = 0;
  int meshCount = 0;
  terrain::SharedPool* poolPtr = pool.get();
  terrain::TestChunkWorker worker(
    [&](int32_t, int32_t, int32_t, uint32_t) {
      ++genCount;
    },
    [&](int32_t) { ++meshCount; }
  );

  terrain::World world(*pool, cfg, worker, nullptr);
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
  REQUIRE(world.getChunk(0, 0)->state == terrain::ChunkState::QueuedGen);
  REQUIRE(world.getChunk(0, 0)->meshRestartRetries == 1);

  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > genAfterInitialLoad);

  world.onWorldGenDone(centerSlot);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  const int meshAfterSecondDispatch = meshCount;
  REQUIRE(meshAfterSecondDispatch > meshAfterFirstDispatch);

  world.onMeshDone(centerSlot, 0, 0, false);
  REQUIRE(world.getChunk(0, 0)->state == terrain::ChunkState::QueuedGen);
  REQUIRE(world.getChunk(0, 0)->meshRestartRetries == 2);

  const int genAfterSecondRestart = genCount;
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount > genAfterSecondRestart);

  world.onWorldGenDone(centerSlot);
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  const int meshAfterThirdDispatch = meshCount;
  REQUIRE(meshAfterThirdDispatch > meshAfterSecondDispatch);

  world.onMeshDone(centerSlot, 0, 0, false);
  REQUIRE(world.getChunk(0, 0)->state == terrain::ChunkState::MeshFailed);
  REQUIRE(world.getChunk(0, 0)->meshRestartRetries == terrain::MAX_CHUNK_MESH_RESTART_RETRIES);

  const int genAfterFailureCap = genCount;
  const int meshAfterFailureCap = meshCount;
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  REQUIRE(genCount == genAfterFailureCap);
  REQUIRE(meshCount == meshAfterFailureCap);
}

TEST_CASE("World queues chunk generation from center outward", "[world]") {
  auto cfg = makeTestConfig();
  cfg.renderDistance = 2;

  auto pool = terrain::SharedPool::create(32, makeDims(cfg));

  std::vector<std::pair<int32_t, int32_t>> generatedChunks;
  terrain::TestChunkWorker worker(
      [&](int32_t, int32_t chunkX, int32_t chunkZ, uint32_t) {
        generatedChunks.emplace_back(chunkX, chunkZ);
      });
  terrain::World world(*pool, cfg, worker, nullptr);

  world.update(glm::vec3(0.0f, 64.0f, 0.0f));

  REQUIRE_FALSE(generatedChunks.empty());
  CHECK(generatedChunks.front() == std::pair<int32_t, int32_t>{0, 0});

  int32_t previousDist2 = -1;
  for (const auto& [chunkX, chunkZ] : generatedChunks) {
    const int32_t dist2 = chunkX * chunkX + chunkZ * chunkZ;
    CHECK(dist2 >= previousDist2);
    previousDist2 = dist2;
  }
}

TEST_CASE("World redstone data get and set", "[world]") {
  auto cfg = makeTestConfig();
  auto pool = terrain::SharedPool::create(16, makeDims(cfg));

  terrain::TestChunkWorker worker;
  terrain::World world(*pool, cfg, worker, nullptr);

  world.update(glm::vec3(0.0f, 64.0f, 0.0f));

  // Center chunk is loaded
  REQUIRE(world.setRedstonePackedAt(1, 10, 1, 42));
  REQUIRE(world.getRedstonePackedAt(1, 10, 1) == 42);

  REQUIRE(world.setRedstonePackedAt(1, 10, 1, 0));
  REQUIRE(world.getRedstonePackedAt(1, 10, 1) == 0);
}

TEST_CASE("ChunkManager basic operations", "[world]") {
  terrain::ChunkManager cm;
  REQUIRE_FALSE(cm.has(0, 0));

  terrain::Chunk c{0, 0, 0};
  cm.set(c);
  REQUIRE(cm.has(0, 0));
  REQUIRE(cm.get(0, 0) != nullptr);
  REQUIRE(cm.get(0, 0)->chunkX == 0);

  cm.remove(0, 0);
  REQUIRE_FALSE(cm.has(0, 0));
}

TEST_CASE("ChunkState transitions", "[world]") {
  terrain::Chunk c{0, 0, 0};
  REQUIRE(c.state == terrain::ChunkState::QueuedGen);

  auto key = c.key();
  REQUIRE(key == terrain::chunkKey(0, 0));
  REQUIRE(terrain::Chunk::makeKey(5, -3) == terrain::chunkKey(5, -3));
}
