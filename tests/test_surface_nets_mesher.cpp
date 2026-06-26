#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/workers/mesher/GreedyMesher.hpp"
#include "world/mesh/SurfaceNetsMesher.hpp"
#include "world/BlockIds.hpp"
#include "world/BlockRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

using namespace voxel;

namespace {

static constexpr int32_t VP_STRIDE = 10;

struct SphereField {
  float cx;
  float cy;
  float cz;
  float radius;
};

struct PlaneField {
  float x0;
};

auto sphereDensity(void* userData, float worldX, float worldY, float worldZ) -> float {
  const auto* field = static_cast<const SphereField*>(userData);
  const float dx = worldX - field->cx;
  const float dy = worldY - field->cy;
  const float dz = worldZ - field->cz;
  return std::sqrt(dx * dx + dy * dy + dz * dz) - field->radius;
}

auto planeDensity(void* userData, float worldX, float, float) -> float {
  const auto* field = static_cast<const PlaneField*>(userData);
  return worldX - field->x0;
}

struct HeightField {
  float y0 = 0.0f;
};

auto heightDensity(void* userData, float, float worldY, float) -> float {
  const auto* field = static_cast<const HeightField*>(userData);
  return worldY - field->y0;
}

struct SeamKey {
  int32_t y = 0;
  int32_t z = 0;

  [[nodiscard]] auto operator<(const SeamKey& other) const -> bool {
    return y < other.y || (y == other.y && z < other.z);
  }

  [[nodiscard]] auto operator==(const SeamKey& other) const -> bool {
    return y == other.y && z == other.z;
  }
};

auto collectSeamKeys(const std::vector<float>& vertices, uint32_t vertexCount, int32_t strideFloats,
                     float originX, float originZ, float seamX) -> std::vector<SeamKey> {
  std::vector<SeamKey> keys;
  for (uint32_t i = 0; i < vertexCount; ++i) {
    const float* v = vertices.data() + static_cast<size_t>(i) * strideFloats;
    const float worldX = originX + v[0];
    if (std::fabs(worldX - seamX) > 1e-3f) continue;
    const int32_t y = static_cast<int32_t>(std::lround(v[1] * 1000.0f));
    const int32_t z = static_cast<int32_t>(std::lround((originZ + v[2]) * 1000.0f));
    keys.push_back(SeamKey{y, z});
  }

  std::sort(keys.begin(), keys.end(), [](const SeamKey& a, const SeamKey& b) {
    return a < b;
  });
  return keys;
}

} // namespace

TEST_CASE("SurfaceNetsMesher extracts a watertight sphere", "[surface-nets][sphere]") {
  mesh::SurfaceNetsConfig cfg;
  cfg.sizeX = 8;
  cfg.sizeY = 8;
  cfg.sizeZ = 8;
  cfg.maxVertices = 2048;
  cfg.maxIndices = 4096;
  cfg.strideFloats = VP_STRIDE;

  SphereField field{3.5f, 3.5f, 3.5f, 2.5f};
  mesh::DensitySampler sampler{};
  sampler.userData = &field;
  sampler.sample = &sphereDensity;

  std::vector<float> vertices(static_cast<size_t>(cfg.maxVertices) * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(static_cast<size_t>(cfg.maxIndices), 0u);
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;

  const bool ok = mesh::surfaceNetsMesh(cfg, sampler, vertices.data(), indices.data(),
                                        vertexCount, indexCount);
  REQUIRE(ok);
  REQUIRE(vertexCount > 0u);
  REQUIRE(indexCount > 0u);
  REQUIRE(indexCount % 3u == 0u);

  std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgeUse;
  for (uint32_t i = 0; i < indexCount; i += 3) {
    const uint32_t a = indices[i + 0];
    const uint32_t b = indices[i + 1];
    const uint32_t c = indices[i + 2];
    auto addEdge = [&](uint32_t u, uint32_t v) {
      if (u > v) std::swap(u, v);
      ++edgeUse[{u, v}];
    };
    addEdge(a, b);
    addEdge(b, c);
    addEdge(c, a);
  }

  for (const auto& [edge, count] : edgeUse) {
    CAPTURE(edge.first, edge.second, count);
    CHECK(count == 2u);
  }

  for (uint32_t i = 0; i < vertexCount; ++i) {
    const float* v = vertices.data() + static_cast<size_t>(i) * VP_STRIDE;
    const float nx = v[3];
    const float ny = v[4];
    const float nz = v[5];
    const float px = v[0] - field.cx;
    const float py = v[1] - field.cy;
    const float pz = v[2] - field.cz;
    const float len = std::sqrt(px * px + py * py + pz * pz);
    REQUIRE(len > 0.0f);
    const float dot = (px * nx + py * ny + pz * nz) / len;
    CHECK(dot > 0.5f);
  }
}

TEST_CASE("SurfaceNetsMesher stays aligned across a shared chunk border",
          "[surface-nets][seam]") {
  mesh::SurfaceNetsConfig cfg;
  cfg.sizeX = 4;
  cfg.sizeY = 6;
  cfg.sizeZ = 4;
  cfg.maxVertices = 1024;
  cfg.maxIndices = 2048;
  cfg.strideFloats = VP_STRIDE;

  PlaneField field{4.0f};
  mesh::DensitySampler sampler{};
  sampler.userData = &field;
  sampler.sample = &planeDensity;

  std::vector<float> vertsA(static_cast<size_t>(cfg.maxVertices) * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indsA(static_cast<size_t>(cfg.maxIndices), 0u);
  std::vector<float> vertsB(static_cast<size_t>(cfg.maxVertices) * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indsB(static_cast<size_t>(cfg.maxIndices), 0u);

  uint32_t vca = 0, ica = 0;
  uint32_t vcb = 0, icb = 0;

  cfg.originX = 0.0f;
  cfg.originY = 0.0f;
  cfg.originZ = 0.0f;
  REQUIRE(mesh::surfaceNetsMesh(cfg, sampler, vertsA.data(), indsA.data(), vca, ica));

  cfg.originX = 4.0f;
  REQUIRE(mesh::surfaceNetsMesh(cfg, sampler, vertsB.data(), indsB.data(), vcb, icb));

  REQUIRE(vcb > 0u);
  REQUIRE(icb > 0u);

  const auto seamA = collectSeamKeys(vertsA, vca, VP_STRIDE, 0.0f, 0.0f, 4.0f);
  const auto seamB = collectSeamKeys(vertsB, vcb, VP_STRIDE, 4.0f, 0.0f, 4.0f);

  CHECK(seamA.empty());
  REQUIRE(!seamB.empty());
  CHECK(vca == 0u);
  CHECK(ica == 0u);
  CHECK(seamB.front().y >= 0);
}

TEST_CASE("SurfaceNetsMesher keeps triangle counts sane on terrain-like fields",
          "[surface-nets][counts]") {
  voxel::BlockRegistry reg(256);
  voxel::BlockDefinition stone;
  stone.id = voxel::BlockId::STONE;
  stone.name = "Stone";
  stone.textures.top = 1;
  stone.textures.bottom = 1;
  stone.textures.side = 1;
  reg.register_(std::move(stone));

  constexpr int32_t SX = 16;
  constexpr int32_t SY = 32;
  constexpr int32_t SZ = 16;

  mesh::SurfaceNetsConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 4096;
  cfg.maxIndices = 8192;
  cfg.strideFloats = VP_STRIDE;

  HeightField field{16.5f};
  mesh::DensitySampler sampler{};
  sampler.userData = &field;
  sampler.sample = &heightDensity;

  std::vector<float> vertices(static_cast<size_t>(cfg.maxVertices) * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(static_cast<size_t>(cfg.maxIndices), 0u);
  uint32_t vertexCount = 0u;
  uint32_t indexCount = 0u;
  REQUIRE(mesh::surfaceNetsMesh(cfg, sampler, vertices.data(), indices.data(),
                                vertexCount, indexCount));
  REQUIRE(vertexCount > 0u);
  REQUIRE(indexCount > 0u);

  std::vector<uint8_t> voxels(static_cast<size_t>(SX) * SY * SZ, 0u);
  for (int32_t y = 0; y < SY; ++y) {
    for (int32_t z = 0; z < SZ; ++z) {
      for (int32_t x = 0; x < SX; ++x) {
        if (static_cast<float>(y) < field.y0) {
          voxels[(y * SZ + z) * SX + x] = voxel::BlockId::STONE;
        }
      }
    }
  }

  voxel::mesher::MesherConfig greedyCfg;
  greedyCfg.sizeX = SX;
  greedyCfg.sizeY = SY;
  greedyCfg.sizeZ = SZ;
  const auto greedyHint = voxel::mesher::estimateMeshCapacity(voxels.data(), reg, greedyCfg);
  const uint32_t surfaceTriangles = indexCount / 3u;
  const uint32_t greedyTriangles = greedyHint.quadCount * 2u;

  CAPTURE(surfaceTriangles, greedyTriangles);
  REQUIRE(greedyTriangles > 0u);
  CHECK(surfaceTriangles <= greedyTriangles);
}
