#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/workers/mesher/GreedyMesher.hpp"
#include "engine/workers/mesher/LightPropagator.hpp"
#include "engine/workers/mesher/SmoothTerrainMesher.hpp"
#include "world/BlockDefinition.hpp"
#include "world/BlockIds.hpp"
#include "world/BlockRegistry.hpp"
#include "world/generation/TerrainSampling.hpp"
#include "world/generation/WorldGenPipeline.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

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

auto registerBlock(voxel::BlockRegistry& reg, uint8_t id,
                   const char* name,
                   uint8_t texTop,
                   uint8_t texBottom,
                   uint8_t texSide,
                   bool opaque = true,
                   bool liquid = false,
                   bool foliage = false) -> void {
  voxel::BlockDefinition def;
  def.id = id;
  def.name = name;
  def.textures.top = texTop;
  def.textures.bottom = texBottom;
  def.textures.side = texSide;
  def.material.opaque = opaque;
  if (!opaque) {
    def.material.transparent = true;
  }
  def.material.liquid = liquid;
  def.material.foliage = foliage;
  reg.register_(std::move(def));
}

auto makeFlatTerrainConfig() -> voxel::WorldGenerationConfig {
  voxel::WorldGenerationConfig cfg;
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

  voxel::TerrainSampler sampler(plains, 42, cfg);
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

TEST_CASE("SmoothTerrainMesher emits a flat plane with sane triangle counts",
          "[terrain][mesher]") {
  FixedClimateSource plains{{0.5f, 0.45f}};
  voxel::WorldGenPipeline pipeline(plains, 42, makeFlatTerrainConfig());

  voxel::BlockRegistry reg(256);
  registerBlock(reg, voxel::BlockId::GRASS, "Grass", 11, 22, 33);
  registerBlock(reg, voxel::BlockId::DIRT, "Dirt", 44, 44, 44);
  registerBlock(reg, voxel::BlockId::STONE, "Stone", 55, 55, 55);
  registerBlock(reg, voxel::BlockId::SAND, "Sand", 66, 66, 66);

  constexpr int32_t SX = 16;
  constexpr int32_t SY = 32;
  constexpr int32_t SZ = 16;

  voxel::mesher::MesherConfig cfg;
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

  const bool ok = voxel::mesher::smoothTerrainMesh(
      pipeline, reg, cfg, 0, 0,
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

  std::vector<uint8_t> voxels(static_cast<size_t>(SX) * SY * SZ, 0u);
  for (int32_t y = 0; y < 16; ++y) {
    for (int32_t z = 0; z < SZ; ++z) {
      for (int32_t x = 0; x < SX; ++x) {
        voxels[(y * SZ + z) * SX + x] = voxel::BlockId::STONE;
      }
    }
  }

  voxel::mesher::MesherConfig greedyCfg = cfg;
  const auto greedyHint = voxel::mesher::estimateMeshCapacity(voxels.data(), reg, greedyCfg);
  const uint32_t smoothTriangles = indexCount / 3u;
  const uint32_t greedyTriangles = greedyHint.quadCount * 2u;

  CAPTURE(smoothTriangles, greedyTriangles);
  REQUIRE(greedyTriangles > 0u);
  CHECK(smoothTriangles <= greedyTriangles * 4u);
}

TEST_CASE("OverlayGreedyMesher hides terrain mass and preserves transparent details",
          "[terrain][overlay][mesher]") {
  voxel::BlockRegistry reg(256);
  registerBlock(reg, voxel::BlockId::STONE, "Stone", 55, 55, 55);
  registerBlock(reg, voxel::BlockId::IRON_ORE, "Iron Ore", 77, 77, 77);
  registerBlock(reg, voxel::BlockId::WATER, "Water", 88, 88, 88, false, true);

  voxel::mesher::MesherConfig cfg;
  cfg.sizeX = 3;
  cfg.sizeY = 3;
  cfg.sizeZ = 3;
  cfg.maxVertices = 256;
  cfg.maxIndices = 512;
  cfg.strideFloats = 10;

  std::vector<float> vertices(static_cast<size_t>(cfg.maxVertices) * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(static_cast<size_t>(cfg.maxIndices), 0u);
  std::vector<uint8_t> light(static_cast<size_t>(cfg.sizeX) * cfg.sizeY * cfg.sizeZ, 0u);

  SECTION("enclosed ore stays hidden behind terrain") {
    std::vector<uint8_t> voxels(static_cast<size_t>(cfg.sizeX) * cfg.sizeY * cfg.sizeZ,
                               voxel::BlockId::STONE);
    voxels[(1 * cfg.sizeZ + 1) * cfg.sizeX + 1] = voxel::BlockId::IRON_ORE;
    voxel::mesher::calculateLighting(voxels.data(), light.data(), reg, cfg);

    uint32_t vertexCount = 0u;
    uint32_t indexCount = 0u;
    uint32_t opaqueCount = 0u;
    uint32_t transparentCount = 0u;
    const bool ok = voxel::mesher::overlayGreedyMesh(
        voxels.data(), light.data(), reg, cfg,
        0u, 0u,
        vertices.data(), indices.data(),
        vertexCount, indexCount,
        nullptr, nullptr,
        &opaqueCount, &transparentCount);

    REQUIRE(ok);
    CHECK(vertexCount == 0u);
    CHECK(indexCount == 0u);
    CHECK(opaqueCount == 0u);
    CHECK(transparentCount == 0u);
  }

  SECTION("exposed solids and liquids still render") {
    std::vector<uint8_t> voxels(static_cast<size_t>(cfg.sizeX) * cfg.sizeY * cfg.sizeZ, 0u);
    voxels[(1 * cfg.sizeZ + 1) * cfg.sizeX + 1] = voxel::BlockId::IRON_ORE;
    voxels[(1 * cfg.sizeZ + 1) * cfg.sizeX + 2] = voxel::BlockId::WATER;
    voxel::mesher::calculateLighting(voxels.data(), light.data(), reg, cfg);

    uint32_t vertexCount = 0u;
    uint32_t indexCount = 0u;
    uint32_t opaqueCount = 0u;
    uint32_t transparentCount = 0u;
    const bool ok = voxel::mesher::overlayGreedyMesh(
        voxels.data(), light.data(), reg, cfg,
        0u, 0u,
        vertices.data(), indices.data(),
        vertexCount, indexCount,
        nullptr, nullptr,
        &opaqueCount, &transparentCount);

    REQUIRE(ok);
    CHECK(vertexCount > 0u);
    CHECK(indexCount > 0u);
    CHECK(opaqueCount > 0u);
    CHECK(transparentCount > 0u);
    CHECK(indexCount == opaqueCount + transparentCount);
  }
}
