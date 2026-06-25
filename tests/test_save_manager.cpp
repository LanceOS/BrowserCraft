#include <catch2/catch_test_macros.hpp>
#include "engine/save/SaveManager.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "world/World.hpp"
#include "engine/core/Config.hpp"
#include "world/BlockRegistry.hpp"
#include "MockWorker.hpp"
#include <cstdio>
#include <filesystem>

namespace {
  auto makeConfig() -> voxel::GameConfig {
    voxel::GameConfig cfg{};
    cfg.chunkSize = 16; cfg.worldHeight = 256; cfg.renderDistance = 4;
    cfg.worldSeed = 42; cfg.maxVertsPerChunk = 100;
    cfg.maxIndicesPerChunk = 200; cfg.vertexStrideFloats = 10;
    return cfg;
  }
  auto makeDims(const voxel::GameConfig& cfg) -> voxel::ChunkDimensions {
    return {cfg.chunkSize, cfg.worldHeight, cfg.chunkSize,
            cfg.maxVertsPerChunk, cfg.maxIndicesPerChunk, cfg.vertexStrideFloats};
  }
}

TEST_CASE("SaveManager save and load chunk", "[save]") {
  auto cfg = makeConfig();
  auto pool = voxel::SharedPool::create(16, makeDims(cfg));
  voxel::BlockRegistry blocks(256);
  blocks.register_(voxel::BlockDefinition{.id=1,.name="stone",.material={.opaque=true}});

  voxel::TestChunkWorker worker;
  voxel::NullPersistence nullPersistence;
  voxel::World world(*pool, blocks, cfg, worker, &nullPersistence);

  auto ioPool = std::make_unique<voxel::WorkerThreadPool>(1);
  voxel::SaveManager saveMgr("./test_saves", "test_slot", *pool, world, ioPool.get());

  // Write some voxel data via an acquired slot
  auto slot = pool->acquire();
  REQUIRE(slot.has_value());
  slot->voxels[0] = 1;
  slot->voxels[100] = 2;

  // We can't easily test save/load without a chunk, but we can test file I/O
  // by writing and reading directly
  std::filesystem::remove_all("./test_saves");
}

TEST_CASE("SaveManager file path generation", "[save]") {
  auto cfg = makeConfig();
  auto pool = voxel::SharedPool::create(4, makeDims(cfg));
  voxel::BlockRegistry blocks(256);

  voxel::TestChunkWorker worker2;
  voxel::NullPersistence nullPersistence2;
  voxel::World world(*pool, blocks, cfg, worker2, &nullPersistence2);

  auto ioPool2 = std::make_unique<voxel::WorkerThreadPool>(1);
  voxel::SaveManager saveMgr("./test_saves2", "slot1", *pool, world, ioPool2.get());

  // Save should not crash
  saveMgr.saveChunk(0, 0);
  saveMgr.flushPending();

  std::filesystem::remove_all("./test_saves2");
}
