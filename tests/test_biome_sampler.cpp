#include <catch2/catch_test_macros.hpp>
#include "content/biomes/BiomeSampler.hpp"

TEST_CASE("BiomeSampler pick returns valid biome", "[biome]") {
  // All biomes should be reachable
  auto& desert = voxel::biome::BiomeSampler::pick(0.8f, 0.2f);
  REQUIRE(desert.name == "desert");

  auto& swamp = voxel::biome::BiomeSampler::pick(0.5f, 0.8f);
  REQUIRE(swamp.name == "swamp");

  auto& mountains = voxel::biome::BiomeSampler::pick(0.1f, 0.5f);
  REQUIRE(mountains.name == "mountains");

  auto& forest = voxel::biome::BiomeSampler::pick(0.5f, 0.7f);
  REQUIRE(forest.name == "forest");

  auto& plains = voxel::biome::BiomeSampler::pick(0.5f, 0.3f);
  REQUIRE(plains.name == "plains");
}

TEST_CASE("BiomeSampler sampleBiome returns valid rule", "[biome]") {
  voxel::biome::BiomeSampler sampler(42);

  auto& rule = sampler.sampleBiome(100.0f, 100.0f);
  REQUIRE(rule.topBlock > 0);
  REQUIRE(rule.depth > 0);
}
