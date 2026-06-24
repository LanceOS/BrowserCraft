#include <catch2/catch_test_macros.hpp>
#include "world/BlockRegistry.hpp"
#include "world/BlockDefinition.hpp"

TEST_CASE("BlockRegistry register and get", "[world]") {
  voxel::BlockRegistry reg(256);

  voxel::BlockDefinition stone{};
  stone.id = 1;
  stone.name = "stone";
  stone.material.opaque = true;

  reg.register_(std::move(stone));

  REQUIRE(reg.tryGet(1) != nullptr);
  REQUIRE(reg.tryGet(1)->name == "stone");
  REQUIRE(reg.tryGet(1)->material.opaque == true);

  REQUIRE(reg.tryGet(99) == nullptr);
}

TEST_CASE("BlockRegistry throws on duplicate", "[world]") {
  voxel::BlockRegistry reg(256);
  reg.register_(voxel::BlockDefinition{.id = 1, .name = "stone"});
  REQUIRE_THROWS(reg.register_(voxel::BlockDefinition{.id = 1, .name = "stone2"}));
}

TEST_CASE("BlockRegistry get throws on unknown", "[world]") {
  voxel::BlockRegistry reg(256);
  REQUIRE_THROWS(reg.get(42));
}

TEST_CASE("BlockRegistry idByName", "[world]") {
  voxel::BlockRegistry reg(256);
  reg.register_(voxel::BlockDefinition{.id = 5, .name = "dirt"});

  auto id = reg.idByName("dirt");
  REQUIRE(id.has_value());
  REQUIRE(*id == 5);

  REQUIRE_FALSE(reg.idByName("nonexistent").has_value());
}

TEST_CASE("BlockRegistry forEach iteration", "[world]") {
  voxel::BlockRegistry reg(256);
  reg.register_(voxel::BlockDefinition{.id = 1, .name = "a"});
  reg.register_(voxel::BlockDefinition{.id = 2, .name = "b"});
  reg.register_(voxel::BlockDefinition{.id = 3, .name = "c"});

  int count = 0;
  reg.forEach([&](const auto&) { ++count; });
  REQUIRE(count == 3);
}
