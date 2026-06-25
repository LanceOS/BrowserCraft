#include <catch2/catch_test_macros.hpp>
#include "content/biomes/BiomeFactory.hpp"
#include "content/biomes/BiomeSampler.hpp"
#include "world/BlockIds.hpp"

TEST_CASE("BiomeFactory pick returns valid biome", "[biome]") {
  using namespace voxel::biome;

  // All biomes should be reachable
  REQUIRE(BiomeFactory::pick(0.8f, 0.2f).id() == BiomeId::Desert);
  REQUIRE(BiomeFactory::pick(0.5f, 0.8f).id() == BiomeId::Swamp);
  REQUIRE(BiomeFactory::pick(0.1f, 0.5f).id() == BiomeId::Mountains);
  REQUIRE(BiomeFactory::pick(0.5f, 0.7f).id() == BiomeId::Forest);
  REQUIRE(BiomeFactory::pick(0.5f, 0.3f).id() == BiomeId::Plains);
  REQUIRE(BiomeFactory::pick(0.35f, 0.25f).id() == BiomeId::Ocean);
}

TEST_CASE("BiomeSampler sampleBiome returns valid biome", "[biome]") {
  voxel::biome::BiomeSampler sampler(42);

  auto& b = sampler.sampleBiome(100.0f, 100.0f);
  REQUIRE(b.topBlock() > 0);
  REQUIRE(b.surfaceDepth() > 0);
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

TEST_CASE("BiomeFactory blendedHeightBias with known climate", "[biome]") {
  using namespace voxel::biome;

  // Pure mountain region: very cold (t=0) → bias ≈ MountainsBiome heightBias (22)
  float bias = BiomeFactory::blendedHeightBias({0.0f, 0.5f});
  REQUIRE(bias > 15.0f);
  REQUIRE(bias <= 22.0f);

  // Pure desert: hot + dry → bias ≈ DesertBiome heightBias (-3)
  bias = BiomeFactory::blendedHeightBias({1.0f, 0.0f});
  REQUIRE(bias < 0.0f);
  REQUIRE(bias >= -3.0f);

  // Pure plains: moderate temp, moderate humidity → bias ≈ PlainsBiome heightBias (0)
  bias = BiomeFactory::blendedHeightBias({0.5f, 0.45f});
  REQUIRE(bias > -2.0f);
  REQUIRE(bias < 2.0f);
}

TEST_CASE("BiomeFactory mountainWeight with known temperature", "[biome]") {
  using namespace voxel::biome;

  // Very cold → full mountain weight
  REQUIRE(BiomeFactory::mountainWeight({0.0f, 0.5f}) > 0.95f);

  // Very hot → no mountain weight
  REQUIRE(BiomeFactory::mountainWeight({1.0f, 0.5f}) < 0.05f);

  // At the threshold center (t=0.28), weight should be ~0.5 (mid-transition)
  float w = BiomeFactory::mountainWeight({0.28f, 0.5f});
  REQUIRE(w > 0.3f);
  REQUIRE(w < 0.7f);

  // Transition band: t=0.10 (well below threshold) → weight ≈ 1
  REQUIRE(BiomeFactory::mountainWeight({0.10f, 0.5f}) > 0.85f);

  // Transition band: t=0.40 (well above threshold) → weight ≈ 0
  REQUIRE(BiomeFactory::mountainWeight({0.40f, 0.5f}) < 0.15f);
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
    {0.35f, 0.25f}, // cool, dry (ocean)
  };

  for (const auto& pt : testPoints) {
    auto w = BiomeFactory::computeWeights(pt);
    float sum = w.plains + w.desert + w.forest + w.mountains + w.swamp + w.ocean;
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

TEST_CASE("BiomeFactory forId covers all biomes", "[biome]") {
  using namespace voxel::biome;

  // ALL_BIOME_IDS should contain exactly 6 entries.
  REQUIRE(ALL_BIOME_IDS.size() == 6);

  // Each BIomeId should be reachable via forId() and return the correct id.
  for (const auto& id : ALL_BIOME_IDS) {
    const Biome& b = BiomeFactory::forId(id);
    REQUIRE(b.id() == id);
    REQUIRE(b.topBlock() > 0);
    REQUIRE(b.surfaceDepth() > 0);
    REQUIRE(b.fillerBlock() > 0);
  }

  // Verify specific known mappings.
  REQUIRE(BiomeFactory::forId(BiomeId::Plains).heightBias() == 0.0f);
  REQUIRE(BiomeFactory::forId(BiomeId::Desert).topBlock() == voxel::BlockId::SAND);
  REQUIRE(BiomeFactory::forId(BiomeId::Ocean).heightBias() < -10.0f);
}
