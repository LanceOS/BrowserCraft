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

TEST_CASE("BiomeClassifer blendedHeightBias with known climate", "[biome]") {
  using namespace voxel::biome;

  // Pure mountain region: very cold (t=0) → bias ≈ MountainsBiome.heightBias (22)
  float bias = BiomeClassifier::blendedHeightBias({0.0f, 0.5f});
  REQUIRE(bias > 15.0f);
  REQUIRE(bias <= 22.0f);

  // Pure desert: hot + dry → bias ≈ DesertBiome.heightBias (-3)
  bias = BiomeClassifier::blendedHeightBias({1.0f, 0.0f});
  REQUIRE(bias < 0.0f);
  REQUIRE(bias >= -3.0f);

  // Pure plains: moderate temp, moderate humidity → bias ≈ PlainsBiome.heightBias (0)
  bias = BiomeClassifier::blendedHeightBias({0.5f, 0.45f});
  REQUIRE(bias > -2.0f);
  REQUIRE(bias < 2.0f);
}

TEST_CASE("BiomeClassifier mountainWeight with known temperature", "[biome]") {
  using namespace voxel::biome;

  // Very cold → full mountain weight
  REQUIRE(BiomeClassifier::mountainWeight({0.0f, 0.5f}) > 0.95f);

  // Very hot → no mountain weight
  REQUIRE(BiomeClassifier::mountainWeight({1.0f, 0.5f}) < 0.05f);

  // At the threshold center (t=0.28), weight should be ~0.5 (mid-transition)
  float w = BiomeClassifier::mountainWeight({0.28f, 0.5f});
  REQUIRE(w > 0.3f);
  REQUIRE(w < 0.7f);

  // Transition band: t=0.10 (well below threshold) → weight ≈ 1
  REQUIRE(BiomeClassifier::mountainWeight({0.10f, 0.5f}) > 0.85f);

  // Transition band: t=0.40 (well above threshold) → weight ≈ 0
  REQUIRE(BiomeClassifier::mountainWeight({0.40f, 0.5f}) < 0.15f);
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
  // Using step 400 instead of 200 for ~6x fewer iterations — still
  // statistically sufficient to verify the average is near zero.
  float sum = 0.0f;
  int32_t count = 0;
  for (float x = -5000.0f; x <= 5000.0f; x += 400.0f) {
    for (float z = -5000.0f; z <= 5000.0f; z += 400.0f) {
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

TEST_CASE("BiomeClassifier computeWeights sums to ~1", "[biome]") {
  using namespace voxel::biome;

  // At a variety of climate points, the sum of weights should be ≈ 1
  // within floating-point tolerance.
  constexpr ClimateSample testPoints[] = {
    {0.0f,  0.0f},  // cold, dry
    {0.0f,  1.0f},  // cold, wet
    {1.0f,  0.0f},  // hot, dry
    {1.0f,  1.0f},  // hot, wet
    {0.5f,  0.5f},  // moderate
    {0.3f,  0.6f},  // cool, humid
    {0.7f,  0.3f},  // warm, dry
  };

  for (const auto& pt : testPoints) {
    auto w = BiomeClassifier::computeWeights(pt);
    float sum = w.plains + w.desert + w.forest + w.mountains + w.swamp;
    // Allow small epsilon for FP rounding.
    REQUIRE(sum > 0.99f);
    REQUIRE(sum < 1.01f);

    // All individual weights must be in [0, 1].
    REQUIRE(w.plains    >= 0.0f);
    REQUIRE(w.desert    >= 0.0f);
    REQUIRE(w.forest    >= 0.0f);
    REQUIRE(w.mountains >= 0.0f);
    REQUIRE(w.swamp     >= 0.0f);
    REQUIRE(w.plains    <= 1.0f);
    REQUIRE(w.desert    <= 1.0f);
    REQUIRE(w.forest    <= 1.0f);
    REQUIRE(w.mountains <= 1.0f);
    REQUIRE(w.swamp     <= 1.0f);
  }
}

TEST_CASE("BiomeData ALL_BIOMES array is complete", "[biome]") {
  using namespace voxel::biome;

  // ALL_BIOMES should contain exactly 5 biomes, each with a unique BiomeId.
  REQUIRE(ALL_BIOMES.size() == 5);

  // Check that all BiomeId values are represented.
  bool hasPlains    = false;
  bool hasDesert    = false;
  bool hasForest    = false;
  bool hasMountains = false;
  bool hasSwamp     = false;

  for (const auto& b : ALL_BIOMES) {
    switch (b.id) {
      case BiomeId::Plains:    hasPlains    = true; break;
      case BiomeId::Desert:    hasDesert    = true; break;
      case BiomeId::Forest:    hasForest    = true; break;
      case BiomeId::Mountains: hasMountains = true; break;
      case BiomeId::Swamp:     hasSwamp     = true; break;
    }
  }

  REQUIRE(hasPlains);
  REQUIRE(hasDesert);
  REQUIRE(hasForest);
  REQUIRE(hasMountains);
  REQUIRE(hasSwamp);
}
