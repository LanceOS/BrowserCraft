#include <catch2/catch_test_macros.hpp>
#include "engine/ecs/EntityManager.hpp"

TEST_CASE("EntityManager allocates unique IDs", "[ecs]") {
  voxel::EntityManager em(100);

  auto a = em.allocate();
  auto b = em.allocate();
  auto c = em.allocate();

  REQUIRE(a != b);
  REQUIRE(b != c);
  REQUIRE(a != c);
  REQUIRE(em.count() == 3);
}

TEST_CASE("EntityManager isAlive and destroy", "[ecs]") {
  voxel::EntityManager em(100);

  auto id = em.allocate();
  REQUIRE(em.isAlive(id));

  em.destroy(id);
  REQUIRE_FALSE(em.isAlive(id));
  REQUIRE(em.count() == 0);

  // Double destroy is safe
  em.destroy(id);
  REQUIRE_FALSE(em.isAlive(id));
}

TEST_CASE("EntityManager reuses indices with generation bump", "[ecs]") {
  voxel::EntityManager em(100);

  auto id1 = em.allocate();
  int32_t idx1 = voxel::EntityManager::indexOf(id1);
  em.destroy(id1);

  auto id2 = em.allocate();
  int32_t idx2 = voxel::EntityManager::indexOf(id2);

  // Same index, different generation
  REQUIRE(idx1 == idx2);
  REQUIRE(id1 != id2);
  REQUIRE_FALSE(em.isAlive(id1));
  REQUIRE(em.isAlive(id2));
}

TEST_CASE("EntityManager capacity exhausted", "[ecs]") {
  voxel::EntityManager em(3);

  em.allocate();
  em.allocate();
  em.allocate();

  REQUIRE_THROWS(em.allocate());
}
