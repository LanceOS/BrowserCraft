#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/math/AABB.hpp"

TEST_CASE("AABB fromChunk computes correct bounds", "[math]") {
  using namespace voxel;
  auto box = AABB::fromChunk(0, 0, 16.0f, 256.0f, 16.0f);
  REQUIRE(box.minX == 0.0f);
  REQUIRE(box.minY == 0.0f);
  REQUIRE(box.minZ == 0.0f);
  REQUIRE(box.maxX == 16.0f);
  REQUIRE(box.maxY == 256.0f);
  REQUIRE(box.maxZ == 16.0f);
}

TEST_CASE("AABB fromChunk with non-zero chunk coords", "[math]") {
  using namespace voxel;
  auto box = AABB::fromChunk(3, -2, 16.0f, 256.0f, 16.0f);
  REQUIRE(box.minX == 48.0f);
  REQUIRE(box.minY == 0.0f);
  REQUIRE(box.minZ == -32.0f);
  REQUIRE(box.maxX == 64.0f);
  REQUIRE(box.maxY == 256.0f);
  REQUIRE(box.maxZ == -16.0f);
}
