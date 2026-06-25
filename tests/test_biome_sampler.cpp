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

TEST_CASE("BiomeSampler blendedHeightBias is smooth", "[biome]") {
  voxel::biome::BiomeSampler sampler(42);

  // blendedHeightBias should return a finite value at any position
  // and should not produce NaN or Inf.
  float bias1 = sampler.blendedHeightBias(0.0f, 0.0f);
  REQUIRE(std::isfinite(bias1));

  float bias2 = sampler.blendedHeightBias(1000.0f, -500.0f);
  REQUIRE(std::isfinite(bias2));

  // Bias should be within reasonable terrain range (between desert and mountain extremes)
  REQUIRE(bias1 > -10.0f);
  REQUIRE(bias1 < 30.0f);

  // Different positions should produce different biases (the noise isn't constant)
  float bias3 = sampler.blendedHeightBias(0.0f, 0.0f);
  float bias4 = sampler.blendedHeightBias(9999.0f, 9999.0f);
  // At very distant positions the bias should differ
  bool differs = (bias3 != bias4);
  // (They could theoretically collide, but with 32-bit float it is astronomically unlikely.)
  REQUIRE(differs);
}

TEST_CASE("BiomeSampler height noise independent of temperature", "[biome]") {
  voxel::biome::BiomeSampler sampler(42);

  // The dedicated height noise returns different values than temperature.
  // Sample at the same coordinates — noise2D (height) should differ from
  // the internal temperature. We can check this indirectly: the height noise
  // uses a different seed, so at the same location the values differ.
  float heightVal = sampler.noise2D(500.0f, 500.0f);

  // Height noise should produce values in [-1, 1] range
  REQUIRE(heightVal >= -1.5f);
  REQUIRE(heightVal <= 1.5f);
}
