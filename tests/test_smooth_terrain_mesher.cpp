#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/workers/mesher/SmoothTerrainMesher.hpp"
#include "world/generation/TerrainSampling.hpp"
#include "world/generation/WorldGenPipeline.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

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
  cfg.baseHeight = 16.5f;
  cfg.continentalScale = 0.0f;
  cfg.continentalAmplitude = 0.0f;
  cfg.detailScale = 0.0f;
  cfg.detailAmplitude = 0.0f;
  cfg.mountainScale = 0.0f;
  cfg.mountainAmplitude = 0.0f;
  cfg.seaLevel = 8;
  cfg.densityNoiseScale = 0.0f;
  cfg.densityDepthScale = 0.0f;
  return cfg;
}

} // namespace

TEST_CASE("TerrainSampler density adds underground cave modulation",
          "[terrain][density][caves]") {
  FixedClimateSource plains{{0.5f, 0.45f}};
  auto cfg = makeFlatTerrainConfig();
  cfg.densityNoiseScale = 0.08f;
  cfg.densityDepthScale = 0.12f;

  terrain::TerrainSampler sampler(plains, 42, cfg);
  const auto terrain = sampler.sampleTerrain(8.0f, 8.0f);

  bool sawModulation = false;
  for (float x = 0.0f; x < 24.0f && !sawModulation; x += 4.0f) {
    for (float z = 0.0f; z < 24.0f && !sawModulation; z += 4.0f) {
      for (float y = terrain.surfaceHeight - 12.0f; y < terrain.surfaceHeight - 5.0f; y += 1.0f) {
        const float baseDensity = y - terrain.surfaceHeight;
        const float density = sampler.sampleDensity(x, y, z);
        if (std::abs(density - baseDensity) > 0.25f) {
          sawModulation = true;
          break;
        }
      }
    }
  }

  REQUIRE(sawModulation);
}

TEST_CASE("SmoothTerrainMesher emits a flat plane",
          "[terrain][mesher]") {
  FixedClimateSource plains{{0.5f, 0.45f}};
  terrain::WorldGenPipeline pipeline(plains, 42, makeFlatTerrainConfig());

  constexpr int32_t SX = 16;
  constexpr int32_t SY = 32;
  constexpr int32_t SZ = 16;

  terrain::mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 50000;
  cfg.maxIndices = 100000;
  cfg.strideFloats = 10;

  std::vector<float> vertices(static_cast<size_t>(cfg.maxVertices) * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(static_cast<size_t>(cfg.maxIndices), 0u);

  uint32_t vertexCount = 0u;
  uint32_t indexCount = 0u;
  uint32_t opaqueCount = 0u;
  uint32_t transparentCount = 0u;

  const bool ok = terrain::mesher::smoothTerrainMesh(
      pipeline, cfg, 0, 0,
      vertices.data(), indices.data(),
      vertexCount, indexCount,
      &opaqueCount, &transparentCount);

  REQUIRE(ok);
  REQUIRE(vertexCount > 0u);
  REQUIRE(indexCount > 0u);
  REQUIRE(indexCount % 3u == 0u);
  REQUIRE(opaqueCount == indexCount);
  REQUIRE(transparentCount == 0u);

  float minY = std::numeric_limits<float>::max();
  float maxY = std::numeric_limits<float>::lowest();
  for (uint32_t vi = 0u; vi < vertexCount; vi += static_cast<uint32_t>(cfg.strideFloats)) {
    minY = std::min(minY, vertices[vi + 1u]);
    maxY = std::max(maxY, vertices[vi + 1u]);
  }

  CHECK(minY == Catch::Approx(16.5f).margin(0.05f));
  CHECK(maxY == Catch::Approx(16.5f).margin(0.05f));
}
