#include <catch2/catch_test_macros.hpp>
#include "engine/ecs/EntityManager.hpp"

TEST_CASE("EntityManager allocates unique IDs", "[ecs]") {
  terrain::EntityManager em(100);

  auto a = em.allocate();
  auto b = em.allocate();
  auto c = em.allocate();

  REQUIRE(a != b);
  REQUIRE(b != c);
  REQUIRE(a != c);
  REQUIRE(em.count() == 3);
}

TEST_CASE("EntityManager isAlive and destroy", "[ecs]") {
  terrain::EntityManager em(100);

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
  terrain::EntityManager em(100);

  auto id1 = em.allocate();
  int32_t idx1 = terrain::EntityManager::indexOf(id1);
  em.destroy(id1);

  auto id2 = em.allocate();
  int32_t idx2 = terrain::EntityManager::indexOf(id2);

  // Same index, different generation
  REQUIRE(idx1 == idx2);
  REQUIRE(id1 != id2);
  REQUIRE_FALSE(em.isAlive(id1));
  REQUIRE(em.isAlive(id2));
}

TEST_CASE("EntityManager capacity exhausted", "[ecs]") {
  terrain::EntityManager em(3);

  auto id1 = em.allocate();
  auto id2 = em.allocate();
  auto id3 = em.allocate();
  REQUIRE(id1 != id2);
  REQUIRE(id2 != id3);
  REQUIRE(id1 != id3);
  (void)id1; (void)id2; (void)id3;

  REQUIRE_THROWS(em.allocate());
}
