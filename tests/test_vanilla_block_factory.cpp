#include <catch2/catch_test_macros.hpp>
#include "world/blocks/VanillaBlockFactory.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockDefinition.hpp"
#include "world/BlockIds.hpp"

TEST_CASE("VanillaBlockFactory registers all blocks from JSON", "[world][blocks]") {
  // This test loads blocks.json from the assets directory and verifies
  // that the factory correctly registers all block definitions.
  voxel::BlockRegistry reg(256);
  voxel::VanillaBlockFactory factory;

  REQUIRE_NOTHROW(factory.registerAll(reg));

  // Core blocks that should always be present
  SECTION("Core blocks exist") {
    CHECK(reg.tryGet(voxel::BlockId::AIR) == nullptr); // Air is skipped
    CHECK(reg.tryGet(voxel::BlockId::STONE) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::DIRT) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::GRASS) != nullptr);
  }

  // New blocks from our expansion
  SECTION("New blocks exist") {
    CHECK(reg.tryGet(voxel::BlockId::OAK_WOOD) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::OAK_PLANKS) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::OAK_LEAVES) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::LAVA) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::DIAMOND_ORE) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::POWERSTONE_ORE) != nullptr);
  }

  // Ores
  SECTION("Ores exist") {
    CHECK(reg.tryGet(voxel::BlockId::COAL_ORE) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::IRON_ORE) != nullptr);
    CHECK(reg.tryGet(voxel::BlockId::GOLD_ORE) != nullptr);
  }
}

TEST_CASE("VanillaBlockFactory sets material properties correctly", "[world][blocks]") {
  voxel::BlockRegistry reg(256);
  voxel::VanillaBlockFactory factory;
  factory.registerAll(reg);

  SECTION("Stone is opaque, not liquid, not foliage, no light") {
    auto* def = reg.tryGet(voxel::BlockId::STONE);
    REQUIRE(def != nullptr);
    CHECK(def->material.opaque == true);
    CHECK(def->material.transparent == false);
    CHECK(def->material.liquid == false);
    CHECK(def->material.foliage == false);
    CHECK(def->material.lightEmission == 0);
  }

  SECTION("Water is liquid, transparent, no light") {
    auto* def = reg.tryGet(voxel::BlockId::WATER);
    REQUIRE(def != nullptr);
    CHECK(def->material.opaque == false);
    CHECK(def->material.transparent == true);
    CHECK(def->material.liquid == true);
    CHECK(def->material.lightEmission == 0);
  }

  SECTION("Lava is liquid, transparent, and emissive") {
    auto* def = reg.tryGet(voxel::BlockId::LAVA);
    REQUIRE(def != nullptr);
    CHECK(def->material.liquid == true);
    CHECK(def->material.transparent == true);
    CHECK(def->material.lightEmission == 15);
  }

  SECTION("Oak Leaves are foliage, transparent") {
    auto* def = reg.tryGet(voxel::BlockId::OAK_LEAVES);
    REQUIRE(def != nullptr);
    CHECK(def->material.opaque == false);
    CHECK(def->material.transparent == true);
    CHECK(def->material.foliage == true);
  }

  SECTION("Powerstone Ore has no light emission") {
    auto* def = reg.tryGet(voxel::BlockId::POWERSTONE_ORE);
    REQUIRE(def != nullptr);
    CHECK(def->material.lightEmission == 0);
  }
}

TEST_CASE("VanillaBlockFactory sets hardness and blast resistance", "[world][blocks]") {
  voxel::BlockRegistry reg(256);
  voxel::VanillaBlockFactory factory;
  factory.registerAll(reg);

  SECTION("Stone has default hardness") {
    auto* def = reg.tryGet(voxel::BlockId::STONE);
    REQUIRE(def != nullptr);
    CHECK(def->hardness == 1.5f);
    CHECK(def->blastResistance == 6.0f);
  }

  SECTION("Oak wood has appropriate values") {
    auto* def = reg.tryGet(voxel::BlockId::OAK_WOOD);
    REQUIRE(def != nullptr);
    CHECK(def->hardness == 2.0f);
  }

  SECTION("Leaves are fragile") {
    auto* def = reg.tryGet(voxel::BlockId::OAK_LEAVES);
    REQUIRE(def != nullptr);
    CHECK(def->hardness == 0.2f);
  }
}

TEST_CASE("VanillaBlockFactory sets texture indices", "[world][blocks]") {
  voxel::BlockRegistry reg(256);
  voxel::VanillaBlockFactory factory;
  factory.registerAll(reg);

  SECTION("Grass has distinct top/bottom/side textures") {
    auto* def = reg.tryGet(voxel::BlockId::GRASS);
    REQUIRE(def != nullptr);
    // Top, bottom, and side should all be different for grass
    CHECK(def->textures.top != def->textures.bottom);
    CHECK(def->textures.side != def->textures.bottom);
  }

  SECTION("Oak Wood has distinct top and side textures") {
    auto* def = reg.tryGet(voxel::BlockId::OAK_WOOD);
    REQUIRE(def != nullptr);
    CHECK(def->textures.top != def->textures.side);
  }

  SECTION("Uniform blocks have same texture on all faces") {
    auto* stone = reg.tryGet(voxel::BlockId::STONE);
    REQUIRE(stone != nullptr);
    CHECK(stone->textures.top == stone->textures.bottom);
    CHECK(stone->textures.top == stone->textures.side);

    auto* planks = reg.tryGet(voxel::BlockId::OAK_PLANKS);
    REQUIRE(planks != nullptr);
    CHECK(planks->textures.top == planks->textures.bottom);
    CHECK(planks->textures.top == planks->textures.side);
  }
}
