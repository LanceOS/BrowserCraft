#include <catch2/catch_test_macros.hpp>
#include "content/biomes/BiomeSampler.hpp"

TEST_CASE("BiomeClassifier pick returns valid biome", "[biome]") {
  using namespace voxel::biome;

  // All biomes should be reachable
  REQUIRE(BiomeClassifier::pick(0.8f, 0.2f).id == BiomeId::Desert);
  REQUIRE(BiomeClassifier::pick(0.5f, 0.8f).id == BiomeId::Swamp);
  REQUIRE(BiomeClassifier::pick(0.1f, 0.5f).id == BiomeId::Mountains);
  REQUIRE(BiomeClassifier::pick(0.5f, 0.7f).id == BiomeId::Forest);
  REQUIRE(BiomeClassifier::pick(0.5f, 0.3f).id == BiomeId::Plains);
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

TEST_CASE("BiomeSampler blended bias stays within biome extremes", "[biome]") {
  voxel::biome::BiomeSampler sampler(42);

  // The blended bias at any position must be bounded by the min and max
  // heightBias across all biomes: DesertBiome (-3) <= bias <= MountainsBiome (22).
  constexpr float minBias = -3.0f;  // DesertBiome.heightBias
  constexpr float maxBias = 22.0f;  // MountainsBiome.heightBias

  // Sample a broad grid to cover various temp/humidity combinations.
  for (float x = -2000.0f; x <= 2000.0f; x += 500.0f) {
    for (float z = -2000.0f; z <= 2000.0f; z += 500.0f) {
      float bias = sampler.blendedHeightBias(x, z);
      REQUIRE(bias >= minBias);
      REQUIRE(bias <= maxBias);
    }
  }

  // The average bias over a large area should be close to plains (0),
  // since plains is the default biome across most of the world.
  float sum = 0.0f;
  int32_t count = 0;
  for (float x = -5000.0f; x <= 5000.0f; x += 200.0f) {
    for (float z = -5000.0f; z <= 5000.0f; z += 200.0f) {
      sum += sampler.blendedHeightBias(x, z);
      ++count;
    }
  }
  float avg = sum / static_cast<float>(count);
  // Plains has heightBias=0. The average across all biomes should be
  // closer to 0 than to the mountain or desert extremes.
  REQUIRE(avg > -5.0f);
  REQUIRE(avg < 5.0f);
}
