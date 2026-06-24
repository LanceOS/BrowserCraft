#include <catch2/catch_test_macros.hpp>
#include "world/generation/WorldGenPipeline.hpp"

TEST_CASE("WorldGenPipeline generates terrain", "[worldgen]") {
  voxel::WorldGenPipeline pipeline(42);

  constexpr int32_t sx = 16, sy = 256, sz = 16;
  std::vector<uint8_t> voxels(sx * sy * sz, 0);

  pipeline.generate(voxels.data(), 0, 0, sx, sy, sz);

  // Check that bedrock is present at y=0
  bool hasBedrock = false;
  for (int32_t x = 0; x < sx; ++x) {
    for (int32_t z = 0; z < sz; ++z) {
      int32_t idx = (0 * sz + z) * sx + x;
      if (voxels[idx] == 7) { hasBedrock = true; break; }
    }
  }
  REQUIRE(hasBedrock);

  // Check that some blocks are generated above bedrock
  int32_t nonAir = 0;
  for (int32_t i = 0; i < sx * sy * sz; ++i) {
    if (voxels[i] != 0) ++nonAir;
  }
  REQUIRE(nonAir > 100);
}

TEST_CASE("WorldGenPipeline handles different seeds", "[worldgen]") {
  voxel::WorldGenPipeline p1(1);
  voxel::WorldGenPipeline p2(99999);

  constexpr int32_t sx = 16, sy = 256, sz = 16;
  std::vector<uint8_t> v1(sx * sy * sz, 0), v2(sx * sy * sz, 0);

  p1.generate(v1.data(), 0, 0, sx, sy, sz);
  p2.generate(v2.data(), 0, 0, sx, sy, sz);

  // Different seeds should produce different terrain
  int32_t differences = 0;
  for (int32_t i = 0; i < sx * sy * sz; ++i) {
    if (v1[i] != v2[i]) ++differences;
  }
  REQUIRE(differences > 50); // significant differences
}
