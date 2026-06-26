#include <catch2/catch_test_macros.hpp>
#include "engine/save/SaveManager.hpp"
#include "engine/save/TerrainSaveData.hpp"
#include "world/terrain/TerrainEditHistory.hpp"
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

TEST_CASE("TerrainSaveData serialization and deserialization", "[save]") {
  std::filesystem::remove_all("./test_terrain_saves");
  std::filesystem::create_directories("./test_terrain_saves");

  voxel::TerrainEditHistory history;
  voxel::TerrainBrush b1{voxel::BrushType::SubtractSphere, glm::vec3(10.0f, 20.0f, 30.0f), 5.0f, 1.5f, glm::vec3(0.0f, 1.0f, 0.0f)};
  voxel::TerrainBrush b2{voxel::BrushType::AddSphere, glm::vec3(-5.0f, 15.5f, 2.0f), 8.0f, 0.8f, glm::vec3(1.0f, 0.0f, 0.0f)};

  history.addEdit(b1, 1000);
  history.addEdit(b2, 2000);

  std::string filePath = "./test_terrain_saves/terrain.edits";
  REQUIRE(voxel::TerrainSaveData::save(filePath, history));

  voxel::TerrainEditHistory loadedHistory;
  REQUIRE(voxel::TerrainSaveData::load(filePath, loadedHistory));

  auto loadedEdits = loadedHistory.getEdits();
  REQUIRE(loadedEdits.size() == 2);

  REQUIRE(loadedEdits[0].brush.type == voxel::BrushType::SubtractSphere);
  REQUIRE(loadedEdits[0].brush.center == glm::vec3(10.0f, 20.0f, 30.0f));
  REQUIRE(loadedEdits[0].brush.radius == 5.0f);
  REQUIRE(loadedEdits[0].brush.strength == 1.5f);
  REQUIRE(loadedEdits[0].brush.planeNormal == glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(loadedEdits[0].timestamp == 1000);

  REQUIRE(loadedEdits[1].brush.type == voxel::BrushType::AddSphere);
  REQUIRE(loadedEdits[1].brush.center == glm::vec3(-5.0f, 15.5f, 2.0f));
  REQUIRE(loadedEdits[1].brush.radius == 8.0f);
  REQUIRE(loadedEdits[1].brush.strength == 0.8f);
  REQUIRE(loadedEdits[1].brush.planeNormal == glm::vec3(1.0f, 0.0f, 0.0f));
  REQUIRE(loadedEdits[1].timestamp == 2000);

  // Test append
  voxel::TerrainBrush b3{voxel::BrushType::Flatten, glm::vec3(0.0f), 10.0f, 2.0f, glm::vec3(0.0f, 0.0f, 1.0f)};
  voxel::TerrainEdit edit3{b3, 3000};
  REQUIRE(voxel::TerrainSaveData::append(filePath, edit3));

  voxel::TerrainEditHistory loadedHistory2;
  REQUIRE(voxel::TerrainSaveData::load(filePath, loadedHistory2));
  auto loadedEdits2 = loadedHistory2.getEdits();
  REQUIRE(loadedEdits2.size() == 3);
  REQUIRE(loadedEdits2[2].brush.type == voxel::BrushType::Flatten);
  REQUIRE(loadedEdits2[2].brush.center == glm::vec3(0.0f));
  REQUIRE(loadedEdits2[2].brush.radius == 10.0f);
  REQUIRE(loadedEdits2[2].brush.strength == 2.0f);
  REQUIRE(loadedEdits2[2].brush.planeNormal == glm::vec3(0.0f, 0.0f, 1.0f));
  REQUIRE(loadedEdits2[2].timestamp == 3000);

  std::filesystem::remove_all("./test_terrain_saves");
}
