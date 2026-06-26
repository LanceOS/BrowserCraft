#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "world/generation/TerrainSampling.hpp"
#include "world/generation/WorldGenPipeline.hpp"

namespace {

class FixedClimateSource final : public voxel::biome::IClimateSource {
public:
  explicit FixedClimateSource(voxel::biome::ClimateSample sample)
    : m_sample(sample) {}

  auto sampleClimate(float, float) const -> voxel::biome::ClimateSample override {
    return m_sample;
  }

private:
  voxel::biome::ClimateSample m_sample;
};

auto makeFlatTerrainConfig() -> voxel::WorldGenerationConfig {
  voxel::WorldGenerationConfig cfg;
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
  voxel::TerrainSampler sampler(plains, 42, makeFlatTerrainConfig());

  const auto terrain = sampler.sampleTerrain(8.0f, 8.0f);

  REQUIRE(sampler.sampleDensity(8.0f, terrain.surfaceHeight - 1.0f, 8.0f) == Catch::Approx(-1.0f));
  REQUIRE(sampler.sampleDensity(8.0f, terrain.surfaceHeight, 8.0f) == Catch::Approx(0.0f));
  REQUIRE(sampler.sampleDensity(8.0f, terrain.surfaceHeight + 1.0f, 8.0f) == Catch::Approx(1.0f));

  const auto grass = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY), 8.0f,
                                            glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(grass.dominant() == voxel::MaterialId::Grass);
  REQUIRE(grass.primary == voxel::MaterialId::Grass);
  REQUIRE(grass.secondary == voxel::MaterialId::Dirt);

  const auto steep = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY), 8.0f,
                                            glm::vec3(0.0f, 0.2f, 0.98f));
  REQUIRE(steep.dominant() == voxel::MaterialId::Stone);

  const auto dirt = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY - 1), 8.0f,
                                           glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(dirt.dominant() == voxel::MaterialId::Dirt);
  REQUIRE(dirt.primary == voxel::MaterialId::Dirt);
  REQUIRE(dirt.secondary == voxel::MaterialId::Stone);

  const auto stone = sampler.sampleMaterial(8.0f, static_cast<float>(terrain.surfaceY - 5), 8.0f,
                                            glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(stone.dominant() == voxel::MaterialId::Stone);
  REQUIRE(stone.primary == voxel::MaterialId::Stone);

  FixedClimateSource desert{{0.8f, 0.2f}};
  voxel::TerrainSampler desertSampler(desert, 42, makeFlatTerrainConfig());
  const auto desertTerrain = desertSampler.sampleTerrain(8.0f, 8.0f);

  const auto desertSurface = desertSampler.sampleMaterial(
      8.0f, static_cast<float>(desertTerrain.surfaceY), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(desertSurface.dominant() == voxel::MaterialId::Sand);
  REQUIRE(desertSurface.primary == voxel::MaterialId::Sand);

  const auto desertShallow = desertSampler.sampleMaterial(
      8.0f, static_cast<float>(desertTerrain.surfaceY - 1), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(desertShallow.dominant() == voxel::MaterialId::Sand);

  const auto desertDeep = desertSampler.sampleMaterial(
      8.0f, static_cast<float>(desertTerrain.surfaceY - 5), 8.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  REQUIRE(desertDeep.dominant() == voxel::MaterialId::Stone);
}

TEST_CASE("WorldGenPipeline sample helpers match the terrain sampler", "[worldgen][terrain]") {
  FixedClimateSource plains{{0.5f, 0.45f}};
  voxel::WorldGenPipeline pipeline(plains, 42, makeFlatTerrainConfig());
  voxel::TerrainSampler sampler(plains, 42, makeFlatTerrainConfig());

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
