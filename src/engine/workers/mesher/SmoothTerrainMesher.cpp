#include "SmoothTerrainMesher.hpp"

#include "LightSampling.hpp"
#include "world/BlockRegistry.hpp"
#include "world/generation/WorldGenPipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {
namespace mesher {
namespace {

struct SmoothTerrainScratch {
  std::vector<float> densitySamples;
};

thread_local SmoothTerrainScratch g_smoothScratch;

struct CornerVertex {
  glm::vec3 pos{};
  float density = 0.0f;
};

struct Triangle {
  glm::vec3 p0{};
  glm::vec3 p1{};
  glm::vec3 p2{};
};

static inline void writeVtx(float* data, uint32_t offset,
                            float x, float y, float z,
                            float nx, float ny, float nz,
                            float primaryMaterial, float secondaryMaterial,
                            float blend, float tint) {
  float* p = data + offset;
  p[0] = x;
  p[1] = y;
  p[2] = z;
  p[3] = nx;
  p[4] = ny;
  p[5] = nz;
  p[6] = primaryMaterial;
  p[7] = secondaryMaterial;
  p[8] = blend;
  p[9] = tint;
}

constexpr std::array<std::array<int32_t, 3>, 8> kCornerOffsets{{
    {{0, 0, 0}},
    {{1, 0, 0}},
    {{1, 1, 0}},
    {{0, 1, 0}},
    {{0, 0, 1}},
    {{1, 0, 1}},
    {{1, 1, 1}},
    {{0, 1, 1}},
}};

constexpr std::array<std::array<int32_t, 4>, 6> kTetrahedra{{
    {{0, 5, 1, 6}},
    {{0, 1, 2, 6}},
    {{0, 2, 3, 6}},
    {{0, 3, 7, 6}},
    {{0, 7, 4, 6}},
    {{0, 4, 5, 6}},
}};

auto densityIndex(int32_t x, int32_t y, int32_t z, const MesherConfig& cfg) -> size_t {
  const size_t sx = static_cast<size_t>(cfg.sizeX + 1);
  const size_t sz = static_cast<size_t>(cfg.sizeZ + 1);
  return (static_cast<size_t>(y) * sz + static_cast<size_t>(z)) * sx + static_cast<size_t>(x);
}

auto safeNormalize(const glm::vec3& v) -> glm::vec3 {
  const float len2 = glm::dot(v, v);
  if (len2 <= std::numeric_limits<float>::epsilon()) {
    return glm::vec3(0.0f, 1.0f, 0.0f);
  }
  return v * (1.0f / std::sqrt(len2));
}

auto interpolateEdge(const CornerVertex& a, const CornerVertex& b) -> glm::vec3 {
  const float denom = a.density - b.density;
  if (std::abs(denom) <= std::numeric_limits<float>::epsilon()) {
    return (a.pos + b.pos) * 0.5f;
  }

  const float t = a.density / (a.density - b.density);
  return a.pos + (b.pos - a.pos) * t;
}

auto cellGradient(const std::array<float, 8>& density) -> glm::vec3 {
  glm::vec3 grad{};
  grad.x = (density[1] + density[2] + density[5] + density[6])
         - (density[0] + density[3] + density[4] + density[7]);
  grad.y = (density[2] + density[3] + density[6] + density[7])
         - (density[0] + density[1] + density[4] + density[5]);
  grad.z = (density[4] + density[5] + density[6] + density[7])
         - (density[0] + density[1] + density[2] + density[3]);
  return safeNormalize(grad);
}

auto orientTriangle(Triangle tri, const glm::vec3& desiredNormal) -> Triangle {
  const glm::vec3 geom = glm::cross(tri.p1 - tri.p0, tri.p2 - tri.p0);
  if (glm::dot(geom, desiredNormal) < 0.0f) {
    std::swap(tri.p1, tri.p2);
  }
  return tri;
}

auto emitTriangle(const Triangle& tri,
                  const glm::vec3& chunkOrigin,
                  const WorldGenPipeline& pipeline,
                  const MesherConfig& cfg,
                  float* vertexOut,
                  uint32_t* indexOut,
                  uint32_t& vertexCountOut,
                  uint32_t& indexCountOut) -> bool {
  if (vertexCountOut + 3u > static_cast<uint32_t>(cfg.maxVertices) ||
      indexCountOut + 3u > static_cast<uint32_t>(cfg.maxIndices)) {
    return false;
  }

  const glm::vec3 worldP0 = tri.p0 + chunkOrigin;
  const glm::vec3 worldP1 = tri.p1 + chunkOrigin;
  const glm::vec3 worldP2 = tri.p2 + chunkOrigin;

  glm::vec3 normal = glm::cross(tri.p1 - tri.p0, tri.p2 - tri.p0);
  normal = safeNormalize(normal);
  if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon()) {
    return true;
  }

  const TerrainMaterial mat0 = pipeline.sampleMaterial(worldP0.x, worldP0.y, worldP0.z, normal);
  const TerrainMaterial mat1 = pipeline.sampleMaterial(worldP1.x, worldP1.y, worldP1.z, normal);
  const TerrainMaterial mat2 = pipeline.sampleMaterial(worldP2.x, worldP2.y, worldP2.z, normal);

  const uint32_t baseVertex = vertexCountOut;
  writeVtx(vertexOut, baseVertex * cfg.strideFloats,
           tri.p0.x, tri.p0.y, tri.p0.z,
           normal.x, normal.y, normal.z,
           static_cast<float>(mat0.primary), static_cast<float>(mat0.secondary),
           mat0.blend, mat0.tint);
  writeVtx(vertexOut, (baseVertex + 1u) * cfg.strideFloats,
           tri.p1.x, tri.p1.y, tri.p1.z,
           normal.x, normal.y, normal.z,
           static_cast<float>(mat1.primary), static_cast<float>(mat1.secondary),
           mat1.blend, mat1.tint);
  writeVtx(vertexOut, (baseVertex + 2u) * cfg.strideFloats,
           tri.p2.x, tri.p2.y, tri.p2.z,
           normal.x, normal.y, normal.z,
           static_cast<float>(mat2.primary), static_cast<float>(mat2.secondary),
           mat2.blend, mat2.tint);

  indexOut[indexCountOut++] = baseVertex;
  indexOut[indexCountOut++] = baseVertex + 1u;
  indexOut[indexCountOut++] = baseVertex + 2u;
  vertexCountOut += 3u;
  return true;
}

auto emitTetra(const std::array<CornerVertex, 4>& corners,
               const glm::vec3& chunkOrigin,
               const glm::vec3& desiredNormal,
               const WorldGenPipeline& pipeline,
               const MesherConfig& cfg,
               float* vertexOut,
               uint32_t* indexOut,
               uint32_t& vertexCountOut,
               uint32_t& indexCountOut) -> bool {
  std::array<int32_t, 4> insideMask{};
  int32_t insideCount = 0;
  for (int32_t i = 0; i < 4; ++i) {
    insideMask[i] = corners[i].density < 0.0f ? 1 : 0;
    insideCount += insideMask[i];
  }

  if (insideCount == 0 || insideCount == 4) {
    return true;
  }

  auto makeTriangle = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> bool {
    Triangle tri{a, b, c};
    tri = orientTriangle(tri, desiredNormal);
    return emitTriangle(tri, chunkOrigin, pipeline, cfg,
                        vertexOut, indexOut, vertexCountOut, indexCountOut);
  };

  if (insideCount == 1 || insideCount == 3) {
    const bool invert = insideCount == 3;
    int32_t minority = -1;
    for (int32_t i = 0; i < 4; ++i) {
      const bool isInside = insideMask[i] != 0;
      if ((!invert && isInside) || (invert && !isInside)) {
        minority = i;
        break;
      }
    }

    if (minority < 0) {
      return true;
    }

    std::array<glm::vec3, 3> points{};
    int32_t pi = 0;
    for (int32_t i = 0; i < 4; ++i) {
      if (i == minority) continue;
      points[pi++] = interpolateEdge(corners[minority], corners[i]);
    }

    if (invert) {
      std::swap(points[1], points[2]);
    }

    return makeTriangle(points[0], points[1], points[2]);
  }

  // Two vertices on each side. Emit a quad as two triangles.
  std::array<glm::vec3, 4> points{};
  int32_t pi = 0;
  for (int32_t i = 0; i < 4; ++i) {
    for (int32_t j = i + 1; j < 4; ++j) {
      if (insideMask[i] == insideMask[j]) continue;
      points[pi++] = interpolateEdge(corners[i], corners[j]);
    }
  }

  if (pi != 4) {
    return true;
  }

  const glm::vec3 centroid = (points[0] + points[1] + points[2] + points[3]) * 0.25f;
  glm::vec3 normal = glm::cross(points[1] - points[0], points[2] - points[0]);
  if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon()) {
    normal = desiredNormal;
  }
  normal = safeNormalize(normal);

  glm::vec3 basisU = points[0] - centroid;
  basisU -= normal * glm::dot(basisU, normal);
  basisU = safeNormalize(basisU);
  glm::vec3 basisV = safeNormalize(glm::cross(normal, basisU));

  std::array<std::pair<float, glm::vec3>, 4> ordered{};
  for (int32_t i = 0; i < 4; ++i) {
    const glm::vec3 delta = points[i] - centroid;
    const float x = glm::dot(delta, basisU);
    const float y = glm::dot(delta, basisV);
    ordered[static_cast<size_t>(i)] = {std::atan2(y, x), points[i]};
  }

  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  return makeTriangle(ordered[0].second, ordered[1].second, ordered[2].second) &&
         makeTriangle(ordered[0].second, ordered[2].second, ordered[3].second);
}

} // namespace

bool smoothTerrainMesh(const WorldGenPipeline& pipeline,
                       const BlockRegistry& blocks,
                       const MesherConfig& cfg,
                       int32_t chunkX,
                       int32_t chunkZ,
                       float* vertexOut,
                       uint32_t* indexOut,
                       uint32_t& vertexCountOut,
                       uint32_t& indexCountOut,
                       uint32_t* opaqueIndexCountOut,
                       uint32_t* transparentIndexCountOut) {
  vertexCountOut = 0u;
  indexCountOut = 0u;
  if (opaqueIndexCountOut) *opaqueIndexCountOut = 0u;
  if (transparentIndexCountOut) *transparentIndexCountOut = 0u;

  if (!vertexOut || !indexOut) return false;
  if (cfg.sizeX <= 0 || cfg.sizeY <= 0 || cfg.sizeZ <= 0) return false;
  if (cfg.maxVertices < 0 || cfg.maxIndices < 0) return false;
  if (cfg.strideFloats < 10) return false;

  const glm::vec3 chunkOrigin(
      static_cast<float>(chunkX * cfg.sizeX),
      0.0f,
      static_cast<float>(chunkZ * cfg.sizeZ));

  const size_t latticeCount =
      static_cast<size_t>(cfg.sizeX + 1) *
      static_cast<size_t>(cfg.sizeY + 1) *
      static_cast<size_t>(cfg.sizeZ + 1);
  if (g_smoothScratch.densitySamples.size() < latticeCount) {
    g_smoothScratch.densitySamples.resize(latticeCount);
  }

  auto sampleAt = [&](int32_t x, int32_t y, int32_t z) -> float {
    const float worldX = static_cast<float>(chunkX * cfg.sizeX + x);
    const float worldY = static_cast<float>(y);
    const float worldZ = static_cast<float>(chunkZ * cfg.sizeZ + z);
    return pipeline.sampleDensity(worldX, worldY, worldZ);
  };

  for (int32_t y = 0; y <= cfg.sizeY; ++y) {
    for (int32_t z = 0; z <= cfg.sizeZ; ++z) {
      for (int32_t x = 0; x <= cfg.sizeX; ++x) {
        g_smoothScratch.densitySamples[densityIndex(x, y, z, cfg)] = sampleAt(x, y, z);
      }
    }
  }

  (void)blocks;

  for (int32_t y = 0; y < cfg.sizeY; ++y) {
    for (int32_t z = 0; z < cfg.sizeZ; ++z) {
      for (int32_t x = 0; x < cfg.sizeX; ++x) {
        std::array<float, 8> cellDensity{};
        std::array<CornerVertex, 8> cellCorners{};

        for (size_t i = 0; i < kCornerOffsets.size(); ++i) {
          const int32_t cx = x + kCornerOffsets[i][0];
          const int32_t cy = y + kCornerOffsets[i][1];
          const int32_t cz = z + kCornerOffsets[i][2];
          cellCorners[i].pos = glm::vec3(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cz));
          cellDensity[i] = g_smoothScratch.densitySamples[densityIndex(cx, cy, cz, cfg)];
          cellCorners[i].density = cellDensity[i];
        }

        const bool hasInside = std::any_of(cellDensity.begin(), cellDensity.end(),
                                           [](float d) { return d < 0.0f; });
        const bool hasOutside = std::any_of(cellDensity.begin(), cellDensity.end(),
                                            [](float d) { return d >= 0.0f; });
        if (!hasInside || !hasOutside) continue;

        const glm::vec3 grad = cellGradient(cellDensity);

        for (const auto& tet : kTetrahedra) {
          std::array<CornerVertex, 4> corners{
              cellCorners[static_cast<size_t>(tet[0])],
              cellCorners[static_cast<size_t>(tet[1])],
              cellCorners[static_cast<size_t>(tet[2])],
              cellCorners[static_cast<size_t>(tet[3])],
          };

          if (!emitTetra(corners, chunkOrigin, grad, pipeline, cfg,
                         vertexOut, indexOut, vertexCountOut, indexCountOut)) {
            if (opaqueIndexCountOut) *opaqueIndexCountOut = indexCountOut;
            if (transparentIndexCountOut) *transparentIndexCountOut = 0u;
            return false;
          }
        }
      }
    }
  }

  if (opaqueIndexCountOut) *opaqueIndexCountOut = indexCountOut;
  if (transparentIndexCountOut) *transparentIndexCountOut = 0u;
  return true;
}

} // namespace mesher
} // namespace voxel
