#include <catch2/catch_test_macros.hpp>
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/TagStore.hpp"
#include "engine/ecs/components/Components.hpp"

TEST_CASE("ComponentStore add and get", "[ecs]") {
  voxel::ComponentStore<voxel::cmp::Transform> store(100);

  int32_t row = store.add(5);
  REQUIRE(row >= 0);

  auto& t = store.get(5);
  t.position = glm::vec3(1.0f, 2.0f, 3.0f);
  t.yaw = 45.0f;

  REQUIRE(store.get(5).position.x == 1.0f);
  REQUIRE(store.get(5).yaw == 45.0f);
}

TEST_CASE("ComponentStore remove and swap", "[ecs]") {
  voxel::ComponentStore<voxel::cmp::Transform> store(100);

  store.add(10);
  store.add(20);
  store.add(30);

  REQUIRE(store.has(20));
  store.remove(20);
  REQUIRE_FALSE(store.has(20));

  // Entity 10 and 30 should still be valid
  REQUIRE(store.has(10));
  REQUIRE(store.has(30));
  REQUIRE(store.count() == 2);
}

TEST_CASE("ComponentStore forEach iteration", "[ecs]") {
  voxel::ComponentStore<voxel::cmp::Health> store(100);

  store.add(1, voxel::cmp::Health{10.0f, 20.0f});
  store.add(3, voxel::cmp::Health{5.0f, 10.0f});
  store.add(7, voxel::cmp::Health{100.0f, 100.0f});

  float sum = 0.0f;
  store.forEach([&](int32_t row, int32_t entity, auto& h) {
    sum += h.current;
  });

  REQUIRE(sum == 115.0f);
  REQUIRE(store.count() == 3);
}

TEST_CASE("TagStore add has remove", "[ecs]") {
  voxel::TagStore tags(100);

  REQUIRE_FALSE(tags.has(42));
  tags.add(42);
  REQUIRE(tags.has(42));
  tags.remove(42);
  REQUIRE_FALSE(tags.has(42));
}

TEST_CASE("ComponentStore tryGet returns pointer", "[ecs]") {
  voxel::ComponentStore<voxel::cmp::Player> store(100);

  store.add(7);
  REQUIRE(store.tryGet(7) != nullptr);
  REQUIRE(store.tryGet(99) == nullptr);
}
