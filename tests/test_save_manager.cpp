#include <catch2/catch_test_macros.hpp>
#include "engine/save/TerrainSaveData.hpp"
#include "engine/save/SaveManager.hpp"
#include "engine/save/WorldMetadata.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/core/Config.hpp"
#include "MockWorker.hpp"
#include "world/World.hpp"
#include "world/terrain/TerrainEditHistory.hpp"
#include <filesystem>

namespace {

auto makeSaveTestConfig() -> terrain::GameConfig {
  terrain::GameConfig cfg{};
  cfg.chunkSize = 16;
  cfg.worldHeight = 256;
  cfg.renderDistance = 2;
  cfg.worldSeed = 0;
  cfg.maxVertsPerChunk = 10000;
  cfg.maxIndicesPerChunk = 20000;
  cfg.vertexStrideFloats = 10;
  return cfg;
}

auto makeSaveTestDims(const terrain::GameConfig& cfg) -> terrain::ChunkDimensions {
  return {
    cfg.chunkSize,
    cfg.worldHeight,
    cfg.chunkSize,
    cfg.maxVertsPerChunk,
    cfg.maxIndicesPerChunk,
    cfg.vertexStrideFloats,
  };
}

} // namespace

TEST_CASE("TerrainSaveData serialization and deserialization", "[save]") {
  std::filesystem::remove_all("./test_terrain_saves");
  std::filesystem::create_directories("./test_terrain_saves");

  terrain::TerrainEditHistory history;
  terrain::TerrainBrush b1{terrain::BrushType::SubtractSphere, glm::vec3(10.0f, 20.0f, 30.0f), 5.0f, 1.5f, glm::vec3(0.0f, 1.0f, 0.0f)};
  terrain::TerrainBrush b2{terrain::BrushType::AddSphere, glm::vec3(-5.0f, 15.5f, 2.0f), 8.0f, 0.8f, glm::vec3(1.0f, 0.0f, 0.0f)};

  history.addEdit(b1, 1000);
  history.addEdit(b2, 2000);

  std::string filePath = "./test_terrain_saves/terrain.edits";
  REQUIRE(terrain::TerrainSaveData::save(filePath, history));

  terrain::TerrainEditHistory loadedHistory;
  REQUIRE(terrain::TerrainSaveData::load(filePath, loadedHistory));

  auto loadedEdits = loadedHistory.getEdits();
  REQUIRE(loadedEdits.size() == 2);

  REQUIRE(loadedEdits[0].brush.type == terrain::BrushType::SubtractSphere);
  REQUIRE(loadedEdits[0].brush.center == glm::vec3(10.0f, 20.0f, 30.0f));
  REQUIRE(loadedEdits[0].brush.radius == 5.0f);
  REQUIRE(loadedEdits[0].brush.strength == 1.5f);
  REQUIRE(loadedEdits[0].brush.planeNormal == glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(loadedEdits[0].timestamp == 1000);

  REQUIRE(loadedEdits[1].brush.type == terrain::BrushType::AddSphere);
  REQUIRE(loadedEdits[1].brush.center == glm::vec3(-5.0f, 15.5f, 2.0f));
  REQUIRE(loadedEdits[1].brush.radius == 8.0f);
  REQUIRE(loadedEdits[1].brush.strength == 0.8f);
  REQUIRE(loadedEdits[1].brush.planeNormal == glm::vec3(1.0f, 0.0f, 0.0f));
  REQUIRE(loadedEdits[1].timestamp == 2000);

  // Test append
  terrain::TerrainBrush b3{terrain::BrushType::Flatten, glm::vec3(0.0f), 10.0f, 2.0f, glm::vec3(0.0f, 0.0f, 1.0f)};
  terrain::TerrainEdit edit3{b3, 3000};
  REQUIRE(terrain::TerrainSaveData::append(filePath, edit3));

  terrain::TerrainEditHistory loadedHistory2;
  REQUIRE(terrain::TerrainSaveData::load(filePath, loadedHistory2));
  auto loadedEdits2 = loadedHistory2.getEdits();
  REQUIRE(loadedEdits2.size() == 3);
  REQUIRE(loadedEdits2[2].brush.type == terrain::BrushType::Flatten);
  REQUIRE(loadedEdits2[2].brush.center == glm::vec3(0.0f));
  REQUIRE(loadedEdits2[2].brush.radius == 10.0f);
  REQUIRE(loadedEdits2[2].brush.strength == 2.0f);
  REQUIRE(loadedEdits2[2].brush.planeNormal == glm::vec3(0.0f, 0.0f, 1.0f));
  REQUIRE(loadedEdits2[2].timestamp == 3000);

  std::filesystem::remove_all("./test_terrain_saves");
}

TEST_CASE("SaveManager loads existing world metadata", "[save][manager]") {
  auto tmpDir = std::filesystem::temp_directory_path() / "terrain_test_save_manager_meta";
  std::filesystem::remove_all(tmpDir);
  std::filesystem::create_directories(tmpDir / "world_slot");

  auto meta = terrain::WorldMetadata::create("World Slot", "world_slot", 987654321u, 1);
  REQUIRE(meta.write(tmpDir / "world_slot" / "world.meta"));

  auto cfg = makeSaveTestConfig();
  auto pool = terrain::SharedPool::create(4, makeSaveTestDims(cfg));
  terrain::TestChunkWorker worker;
  terrain::World world(*pool, cfg, worker, nullptr);

  terrain::SaveManager saveManager(tmpDir.string(), "world_slot", *pool, world, nullptr);

  REQUIRE(saveManager.metadata().displayName() == "World Slot");
  REQUIRE(saveManager.metadata().displaySlug() == "world_slot");
  REQUIRE(saveManager.metadata().seed == 987654321u);
  REQUIRE(saveManager.metadata().gameMode == 1);

  std::filesystem::remove_all(tmpDir);
}
