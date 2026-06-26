#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "world/generation/TerrainSampling.hpp"
#include "world/generation/WorldGenPipeline.hpp"

namespace {

class FixedClimateSource final : public terrain::biome::IClimateSource {
public:
  explicit FixedClimateSource(terrain::biome::ClimateSample sample)
    : m_sample(sample) {}

  auto sampleClimate(float, float) const -> terrain::biome::ClimateSample override {
    return m_sample;
  }

private:
  terrain::biome::ClimateSample m_sample;
};

auto makeFlatTerrainConfig() -> terrain::WorldGenerationConfig {
  terrain::WorldGenerationConfig cfg;
  cfg.baseHeight = 32.0f;
  cfg.continentalScale = 0.0f;
  cfg.continentalAmplitude = 0.0f;
  cfg.detailScale = 0.0f;
  cfg.detailAmplitude = 0.0f;
  cfg.mountainScale = 0.0f;
  cfg.mountainAmplitude = 0.0f;
  cfg.seaLevel = 16;
  cfg.densityNoiseScale = 0.0f;
  cfg.densityDepthScale = 0.0f;
  return cfg;
}

} // namespace

TEST_CASE("TerrainSampler exposes continuous density and layered materials", "[worldgen][terrain]") {
  FixedClimateSource plains{{0.5f, 0.45f}};
  terrain::TerrainSampler sampler(plains, 42, makeFlatTerrainConfig());

  const auto terrain = sampler.sampleTerrain(8.0f, 8.0f);

  REQUIRE(sampler.sampleDensity(8.0f, terrain.surfaceHeight - 1.0f, 8.0f) == Catch::Approx(-1.0f));
  REQUIRE(sampler.sampleDensity(8.0f, terrain.surfaceHeight, 8.0f) == Catch::Approx(0.0f));
  REQUIRE(sampler.sampleDensity(8.0f, terrain.surfaceHeight + 1.0f, 8.0f) == Catch::Approx(1.0f));

  const auto grass = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY), 8.0f,
                                            glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(grass.dominant() == terrain::MaterialId::Grass);
  REQUIRE(grass.primary == terrain::MaterialId::Grass);
  REQUIRE(grass.secondary == terrain::MaterialId::Dirt);

  const auto steep = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY), 8.0f,
                                            glm::vec3(0.0f, 0.2f, 0.98f));
  REQUIRE(steep.dominant() == terrain::MaterialId::Stone);

  const auto dirt = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY - 1), 8.0f,
                                           glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(dirt.dominant() == terrain::MaterialId::Dirt);
  REQUIRE(dirt.primary == terrain::MaterialId::Dirt);
  REQUIRE(dirt.secondary == terrain::MaterialId::Stone);

  const auto stone = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY - 5), 8.0f,
                                            glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(stone.dominant() == terrain::MaterialId::Stone);
  REQUIRE(stone.primary == terrain::MaterialId::Stone);

  FixedClimateSource desert{{0.8f, 0.2f}};
  terrain::TerrainSampler desertSampler(desert, 42, makeFlatTerrainConfig());
  const auto desertTerrain = desertSampler.sampleTerrain(8.0f, 8.0f);

  const auto desertSurface = desertSampler.sampleMaterial(
      8.0f, static_cast<float>(desertTerrain.surfaceY), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(desertSurface.dominant() == terrain::MaterialId::Sand);
  REQUIRE(desertSurface.primary == terrain::MaterialId::Sand);

  const auto desertShallow = desertSampler.sampleMaterial(
      8.0f, static_cast<float>(desertTerrain.surfaceY - 1), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(desertShallow.dominant() == terrain::MaterialId::Sand);

  const auto desertDeep = desertSampler.sampleMaterial(
      8.0f, static_cast<float>(desertTerrain.surfaceY - 5), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(desertDeep.dominant() == terrain::MaterialId::Stone);
}

TEST_CASE("WorldGenPipeline sample helpers match the terrain sampler", "[worldgen][terrain]") {
  FixedClimateSource plains{{0.5f, 0.45f}};
  terrain::WorldGenPipeline pipeline(plains, 42, makeFlatTerrainConfig());
  terrain::TerrainSampler sampler(plains, 42, makeFlatTerrainConfig());

  const auto terrain = sampler.sampleTerrain(8.0f, 8.0f);

  REQUIRE(pipeline.sampleDensity(8.0f, terrain.surfaceHeight - 1.0f, 8.0f)
          == Catch::Approx(sampler.sampleDensity(8.0f, terrain.surfaceHeight - 1.0f, 8.0f)));
  REQUIRE(pipeline.sampleDensity(8.0f, terrain.surfaceHeight, 8.0f)
          == Catch::Approx(sampler.sampleDensity(8.0f, terrain.surfaceHeight, 8.0f)));
  REQUIRE(pipeline.sampleDensity(8.0f, terrain.surfaceHeight + 1.0f, 8.0f)
          == Catch::Approx(sampler.sampleDensity(8.0f, terrain.surfaceHeight + 1.0f, 8.0f)));

  const auto pipelineGrass = pipeline.sampleMaterial(
      8.0f, static_cast<float>(terrain.surfaceY), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  const auto samplerGrass = sampler.sampleMaterial(
      8.0f, static_cast<float>(terrain.surfaceY), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(pipelineGrass.dominant() == samplerGrass.dominant());
  REQUIRE(pipelineGrass.blend == Catch::Approx(samplerGrass.blend));

  const auto pipelineDirt = pipeline.sampleMaterial(
      8.0f, static_cast<float>(terrain.surfaceY - 1), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  const auto samplerDirt = sampler.sampleMaterial(
      8.0f, static_cast<float>(terrain.surfaceY - 1), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(pipelineDirt.dominant() == samplerDirt.dominant());
  REQUIRE(pipelineDirt.blend == Catch::Approx(samplerDirt.blend));

  const auto pipelineStone = pipeline.sampleMaterial(
      8.0f, static_cast<float>(terrain.surfaceY - 5), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  const auto samplerStone = sampler.sampleMaterial(
      8.0f, static_cast<float>(terrain.surfaceY - 5), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(pipelineStone.dominant() == samplerStone.dominant());
  REQUIRE(pipelineStone.blend == Catch::Approx(samplerStone.blend));
}

TEST_CASE("WorldGenPipeline continuous sampling validity", "[worldgen]") {
  terrain::WorldGenPipeline pipeline(42);

  // Sample continuous density at a few heights and verify variation
  float dHigh = pipeline.sampleDensity(0.0f, 100.0f, 0.0f);
  float dLow = pipeline.sampleDensity(0.0f, 5.0f, 0.0f);

  // Above ground should generally be positive (air), deep below ground should be negative (solid)
  CHECK(dHigh > 0.0f);
  CHECK(dLow < 0.0f);
}

TEST_CASE("WorldGenPipeline handles different seeds for continuous sampling", "[worldgen]") {
  terrain::WorldGenPipeline p1(1);
  terrain::WorldGenPipeline p2(99999);

  // Different seeds should produce different density values
  float d1 = p1.sampleDensity(10.0f, 32.0f, 10.0f);
  float d2 = p2.sampleDensity(10.0f, 32.0f, 10.0f);

  REQUIRE(d1 != Catch::Approx(d2).margin(1e-5f));
}
