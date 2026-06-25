#include <catch2/catch_test_macros.hpp>
#include "content/flora/DefaultFlora.hpp"
#include "content/flora/FloraRegistry.hpp"
#include "content/flora/GrassFactory.hpp"
#include "content/flora/FloraTypes.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockDefinition.hpp"
#include "world/BlockIds.hpp"
#include "world/blocks/VanillaBlockFactory.hpp"

TEST_CASE("FloraRegistry can be created empty", "[flora]") {
  voxel::flora::FloraRegistry registry;
  CHECK(registry.getAllCategories().empty());
  CHECK(registry.getCategory("grass") == nullptr);
  CHECK_FALSE(registry.isFlora(1));
}

TEST_CASE("FloraRegistry registers and looks up categories", "[flora]") {
  voxel::flora::FloraRegistry registry;

  auto grassFactory = std::make_unique<voxel::flora::GrassFactory>(
    std::vector<voxel::flora::GrassSpeciesDefinition>{});
  registry.registerCategory(std::move(grassFactory));

  REQUIRE(registry.getCategory("grass") != nullptr);
  CHECK(registry.getAllCategories().size() == 1);
}

TEST_CASE("DefaultFloraRegistry creates grass categories", "[flora]") {
  auto registry = voxel::flora::createDefaultFloraRegistry();

  REQUIRE(registry != nullptr);
  CHECK(registry->getCategory("grass") != nullptr);

  // Verify grass species are registered in the factory
  auto* grassFactory = registry->getCategory("grass");
  REQUIRE(grassFactory != nullptr);
  auto allProps = grassFactory->getAllProperties();
  CHECK(allProps.size() >= 2); // Tall Grass + Fern
}

TEST_CASE("FloraRegistry registers blocks with BlockRegistry", "[flora][integration]") {
  voxel::BlockRegistry blockReg(256);

  // First register vanilla blocks (including grass blocks from JSON)
  {
    voxel::VanillaBlockFactory factory;
    factory.registerAll(blockReg);
  }

  // Then register flora
  auto floraReg = voxel::flora::createDefaultFloraRegistry();
  floraReg->registerAllBlocks(blockReg);

  // Tall Grass and Fern should be registered
  auto* tallGrass = blockReg.tryGet(voxel::BlockId::TALL_GRASS);
  REQUIRE(tallGrass != nullptr);
  CHECK(tallGrass->name == "Tall Grass");
  CHECK(tallGrass->material.opaque == false);
  CHECK(tallGrass->material.foliage == true);

  auto* fern = blockReg.tryGet(voxel::BlockId::FERN);
  REQUIRE(fern != nullptr);
  CHECK(fern->name == "Fern");
}

TEST_CASE("GrassFactory.getProperties returns correct metadata", "[flora]") {
  voxel::flora::GrassFactory factory(std::vector<voxel::flora::GrassSpeciesDefinition>{
    {
      .speciesId = 0,
      .name = "Tall Grass",
      .blockId = 17,
      .textureLayer = 0,
      .acceptableSoil = {voxel::flora::SoilType::GRASS},
      .spawnChance = 0.1f,
      .isTall = false,
      .dropsSeeds = true,
      .seedItemId = 17,
    },
  });

  auto props = factory.getProperties(0);
  CHECK(props.blockId == 17);
  CHECK(props.name == "Tall Grass");
  CHECK(props.renderType == voxel::flora::FloraRenderType::CROSS_QUAD);
  CHECK(props.acceptableSoil.size() == 1);
  CHECK(props.acceptableSoil[0] == voxel::flora::SoilType::GRASS);
  CHECK(props.dropsSelf == true);
  CHECK(props.dropItemId == 17);
  CHECK(props.boneMealable == false);

  // Unknown species should throw
  CHECK_THROWS(factory.getProperties(999));
}

TEST_CASE("GrassFactory handles tall grass species", "[flora]") {
  voxel::flora::GrassFactory factory(std::vector<voxel::flora::GrassSpeciesDefinition>{
    {
      .speciesId = 0,
      .name = "Tall Grass",
      .blockId = 17,
      .textureLayer = 0,
      .acceptableSoil = {voxel::flora::SoilType::GRASS},
      .spawnChance = 0.1f,
      .isTall = true, // Key difference
      .dropsSeeds = true,
      .seedItemId = 17,
    },
  });

  auto props = factory.getProperties(0);
  CHECK(props.renderType == voxel::flora::FloraRenderType::TALL_CROSS_QUAD);
}

TEST_CASE("GrassFactory skips already-registered blocks", "[flora]") {
  // Pre-register a block with ID 17
  voxel::BlockRegistry reg(256);
  voxel::BlockDefinition existing;
  existing.id = 17;
  existing.name = "Pre-registered";
  existing.material.opaque = true;
  reg.register_(std::move(existing));

  // GrassFactory should not overwrite it
  voxel::flora::GrassFactory factory(std::vector<voxel::flora::GrassSpeciesDefinition>{
    {
      .speciesId = 0,
      .name = "Tall Grass",
      .blockId = 17,
      .textureLayer = 0,
      .acceptableSoil = {voxel::flora::SoilType::GRASS},
      .spawnChance = 0.1f,
      .isTall = false,
      .dropsSeeds = true,
      .seedItemId = 17,
    },
  });

  REQUIRE_NOTHROW(factory.registerBlocks(reg));

  // Should still be the pre-registered block
  auto* def = reg.tryGet(17);
  REQUIRE(def != nullptr);
  CHECK(def->name == "Pre-registered");
}
