#include <catch2/catch_test_macros.hpp>
#include "engine/ecs/Query.hpp"
#include "engine/ecs/components/Components.hpp"

TEST_CASE("queryEntities finds intersection", "[ecs]") {
  terrain::ComponentStore<terrain::cmp::Transform> transforms(100);
  terrain::ComponentStore<terrain::cmp::Health> healths(100);

  transforms.add(1);
  transforms.add(2);
  transforms.add(3);

  healths.add(2);
  healths.add(3);
  healths.add(5);

  auto result = terrain::queryEntities(transforms, healths);

  // Only entities 2 and 3 have both
  REQUIRE(result.size() == 2);
  // Entity 2 and 3 should be in result (order may vary)
  bool has2 = false, has3 = false;
  for (auto e : result) {
    if (e == 2) has2 = true;
    if (e == 3) has3 = true;
  }
  REQUIRE(has2);
  REQUIRE(has3);
}

TEST_CASE("queryEntities empty when no overlap", "[ecs]") {
  terrain::ComponentStore<terrain::cmp::Transform> transforms(100);
  terrain::ComponentStore<terrain::cmp::Health> healths(100);

  transforms.add(1);
  healths.add(2);

  auto result = terrain::queryEntities(transforms, healths);
  REQUIRE(result.empty());
}

TEST_CASE("queryEntities single store returns all", "[ecs]") {
  terrain::ComponentStore<terrain::cmp::Transform> transforms(100);

  transforms.add(10);
  transforms.add(20);

  auto result = terrain::queryEntities(transforms);
  REQUIRE(result.size() == 2);
}

TEST_CASE("queryEntities with TagStore", "[ecs]") {
  terrain::ComponentStore<terrain::cmp::Transform> transforms(100);
  terrain::TagStore hostile(100);

  transforms.add(1);
  transforms.add(2);
  transforms.add(3);

  hostile.add(2);
  hostile.add(3);

  auto result = terrain::queryEntities(transforms, hostile);
  REQUIRE(result.size() == 2);
}
