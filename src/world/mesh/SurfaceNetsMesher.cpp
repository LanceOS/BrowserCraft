#include "SurfaceNetsMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel::mesh {

namespace {

constexpr uint32_t kPackedVertexFloats = 10u;
constexpr float kDefaultSkyLight = 15.0f;
constexpr float kUvScale = 0.25f;

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct SurfaceNetsScratch {
  std::vector<float> densities;
  std::vector<Vec3> gradients;
  std::vector<uint32_t> cellVertices;
};

thread_local SurfaceNetsScratch g_scratch;

inline auto makeVec3(float x, float y, float z) -> Vec3 {
  return Vec3{x, y, z};
}

inline auto add(const Vec3& a, const Vec3& b) -> Vec3 {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline auto sub(const Vec3& a, const Vec3& b) -> Vec3 {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline auto mul(const Vec3& v, float s) -> Vec3 {
  return Vec3{v.x * s, v.y * s, v.z * s};
}

inline auto dot(const Vec3& a, const Vec3& b) -> float {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline auto cross(const Vec3& a, const Vec3& b) -> Vec3 {
  return Vec3{
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

inline auto lengthSquared(const Vec3& v) -> float {
  return dot(v, v);
}

inline auto normalize(const Vec3& v) -> Vec3 {
  const float lenSq = lengthSquared(v);
  if (lenSq <= 1e-20f) {
    return Vec3{0.0f, 1.0f, 0.0f};
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  return mul(v, invLen);
}

inline auto clampCoord(int32_t value, int32_t lo, int32_t hi) -> int32_t {
  return std::max(lo, std::min(hi, value));
}

inline auto vertexOffset(uint32_t vertexIndex, uint32_t strideFloats) -> size_t {
  return static_cast<size_t>(vertexIndex) * static_cast<size_t>(strideFloats);
}

inline void writeVertex(float* out, uint32_t vertexIndex, uint32_t strideFloats,
                        const Vec3& pos, const Vec3& normal) {
  float* p = out + vertexOffset(vertexIndex, strideFloats);
  p[0] = pos.x;
  p[1] = pos.y;
  p[2] = pos.z;
  p[3] = normal.x;
  p[4] = normal.y;
  p[5] = normal.z;

  // Simple projected UVs keep the terrain readable without a dedicated terrain shader.
  const float ax = std::fabs(normal.x);
  const float ay = std::fabs(normal.y);
  const float az = std::fabs(normal.z);
  float u = 0.0f;
  float v = 0.0f;
  if (ay >= ax && ay >= az) {
    u = pos.x * kUvScale;
    v = pos.z * kUvScale;
  } else if (ax >= az) {
    u = pos.z * kUvScale;
    v = pos.y * kUvScale;
  } else {
    u = pos.x * kUvScale;
    v = pos.y * kUvScale;
  }

  p[6] = u;
  p[7] = v;
  p[8] = 0.0f;
  p[9] = kDefaultSkyLight;
}

inline auto sampleIndex(int32_t x, int32_t y, int32_t z,
                        int32_t sampleXCount, int32_t sampleZCount) -> size_t {
  return (static_cast<size_t>(y) * static_cast<size_t>(sampleZCount) +
          static_cast<size_t>(z)) * static_cast<size_t>(sampleXCount) +
         static_cast<size_t>(x);
}

inline auto cellIndex(int32_t x, int32_t y, int32_t z,
                      int32_t cellXCount, int32_t cellZCount) -> size_t {
  return (static_cast<size_t>(y) * static_cast<size_t>(cellZCount) +
          static_cast<size_t>(z)) * static_cast<size_t>(cellXCount) +
         static_cast<size_t>(x);
}

} // namespace

auto surfaceNetsMesh(
    const SurfaceNetsConfig& cfg,
    const DensitySampler& density,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut) -> bool
{
  vertexCountOut = 0u;
  indexCountOut = 0u;

  if (!density.valid() || !vertexOut || !indexOut) return false;
  if (cfg.sizeX <= 0 || cfg.sizeY <= 0 || cfg.sizeZ <= 0) return false;
  if (cfg.maxVertices < 0 || cfg.maxIndices < 0) return false;
  if (cfg.strideFloats < static_cast<int32_t>(kPackedVertexFloats)) return false;

  const int32_t sx = cfg.sizeX;
  const int32_t sy = cfg.sizeY;
  const int32_t sz = cfg.sizeZ;
  const int32_t strideFloats = cfg.strideFloats;

  // Negative X/Z border halo keeps the seam owned by the positive-side chunk
  // while still letting us stitch border triangles without cracks.
  const int32_t sampleMinX = -1;
  const int32_t sampleMaxX = sx;
  const int32_t sampleMinY = 0;
  const int32_t sampleMaxY = sy;
  const int32_t sampleMinZ = -1;
  const int32_t sampleMaxZ = sz;

  const int32_t sampleXCount = sx + 2;
  const int32_t sampleYCount = sy + 1;
  const int32_t sampleZCount = sz + 2;
  const size_t sampleCount = static_cast<size_t>(sampleXCount) *
                             static_cast<size_t>(sampleYCount) *
                             static_cast<size_t>(sampleZCount);

  const int32_t cellXCount = sx + 1;
  const int32_t cellYCount = sy;
  const int32_t cellZCount = sz + 1;
  const size_t cellCount = static_cast<size_t>(cellXCount) *
                           static_cast<size_t>(cellYCount) *
                           static_cast<size_t>(cellZCount);

  if (g_scratch.densities.size() < sampleCount) g_scratch.densities.resize(sampleCount);
  if (g_scratch.gradients.size() < sampleCount) g_scratch.gradients.resize(sampleCount);
  if (g_scratch.cellVertices.size() < cellCount) g_scratch.cellVertices.resize(cellCount);

  auto sampleAt = [&](int32_t x, int32_t y, int32_t z) -> float {
    x = clampCoord(x, sampleMinX, sampleMaxX);
    y = clampCoord(y, sampleMinY, sampleMaxY);
    z = clampCoord(z, sampleMinZ, sampleMaxZ);
    const size_t idx = sampleIndex(x - sampleMinX, y - sampleMinY, z - sampleMinZ,
                                   sampleXCount, sampleZCount);
    return g_scratch.densities[idx];
  };

  // Sample the density lattice in world coordinates.
  for (int32_t y = sampleMinY; y <= sampleMaxY; ++y) {
    const float worldY = cfg.originY + static_cast<float>(y);
    for (int32_t z = sampleMinZ; z <= sampleMaxZ; ++z) {
      const float worldZ = cfg.originZ + static_cast<float>(z);
      for (int32_t x = sampleMinX; x <= sampleMaxX; ++x) {
        const float worldX = cfg.originX + static_cast<float>(x);
        const size_t idx = sampleIndex(x - sampleMinX, y - sampleMinY, z - sampleMinZ,
                                       sampleXCount, sampleZCount);
        g_scratch.densities[idx] = density(worldX, worldY, worldZ);
      }
    }
  }

  // Central-difference gradients on the sampled lattice.
  for (int32_t y = sampleMinY; y <= sampleMaxY; ++y) {
    for (int32_t z = sampleMinZ; z <= sampleMaxZ; ++z) {
      for (int32_t x = sampleMinX; x <= sampleMaxX; ++x) {
        const int32_t x0 = clampCoord(x - 1, sampleMinX, sampleMaxX);
        const int32_t x1 = clampCoord(x + 1, sampleMinX, sampleMaxX);
        const int32_t y0 = clampCoord(y - 1, sampleMinY, sampleMaxY);
        const int32_t y1 = clampCoord(y + 1, sampleMinY, sampleMaxY);
        const int32_t z0 = clampCoord(z - 1, sampleMinZ, sampleMaxZ);
        const int32_t z1 = clampCoord(z + 1, sampleMinZ, sampleMaxZ);

        const float dx = (x0 == x1)
          ? 0.0f
          : (sampleAt(x1, y, z) - sampleAt(x0, y, z)) / static_cast<float>(x1 - x0);
        const float dy = (y0 == y1)
          ? 0.0f
          : (sampleAt(x, y1, z) - sampleAt(x, y0, z)) / static_cast<float>(y1 - y0);
        const float dz = (z0 == z1)
          ? 0.0f
          : (sampleAt(x, y, z1) - sampleAt(x, y, z0)) / static_cast<float>(z1 - z0);

        const size_t idx = sampleIndex(x - sampleMinX, y - sampleMinY, z - sampleMinZ,
                                       sampleXCount, sampleZCount);
        g_scratch.gradients[idx] = Vec3{dx, dy, dz};
      }
    }
  }

  std::fill(g_scratch.cellVertices.begin(), g_scratch.cellVertices.begin() + cellCount,
            std::numeric_limits<uint32_t>::max());

  constexpr std::array<std::array<int32_t, 3>, 8> cornerOffsets = {{
    {{0, 0, 0}}, {{1, 0, 0}}, {{1, 1, 0}}, {{0, 1, 0}},
    {{0, 0, 1}}, {{1, 0, 1}}, {{1, 1, 1}}, {{0, 1, 1}},
  }};

  constexpr std::array<std::array<int32_t, 2>, 12> edgeCorners = {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
    {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
    {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
  }};

  auto gradientAt = [&](int32_t x, int32_t y, int32_t z) -> Vec3 {
    const size_t idx = sampleIndex(x - sampleMinX, y - sampleMinY, z - sampleMinZ,
                                   sampleXCount, sampleZCount);
    return g_scratch.gradients[idx];
  };

  auto interpolateGradient = [&](int32_t cellX, int32_t cellY, int32_t cellZ,
                                 float fx, float fy, float fz) -> Vec3 {
    const Vec3 g000 = gradientAt(cellX,     cellY,     cellZ);
    const Vec3 g100 = gradientAt(cellX + 1, cellY,     cellZ);
    const Vec3 g110 = gradientAt(cellX + 1, cellY + 1, cellZ);
    const Vec3 g010 = gradientAt(cellX,     cellY + 1, cellZ);
    const Vec3 g001 = gradientAt(cellX,     cellY,     cellZ + 1);
    const Vec3 g101 = gradientAt(cellX + 1, cellY,     cellZ + 1);
    const Vec3 g111 = gradientAt(cellX + 1, cellY + 1, cellZ + 1);
    const Vec3 g011 = gradientAt(cellX,     cellY + 1, cellZ + 1);

    const Vec3 gx0 = add(mul(g000, 1.0f - fx), mul(g100, fx));
    const Vec3 gx1 = add(mul(g010, 1.0f - fx), mul(g110, fx));
    const Vec3 gx2 = add(mul(g001, 1.0f - fx), mul(g101, fx));
    const Vec3 gx3 = add(mul(g011, 1.0f - fx), mul(g111, fx));
    const Vec3 gy0 = add(mul(gx0, 1.0f - fy), mul(gx1, fy));
    const Vec3 gy1 = add(mul(gx2, 1.0f - fy), mul(gx3, fy));
    return add(mul(gy0, 1.0f - fz), mul(gy1, fz));
  };

  // Create one dual vertex per active cell.
  uint32_t vertexCount = 0u;
  uint32_t indexCount = 0u;
  for (int32_t cellY = 0; cellY < cellYCount; ++cellY) {
    for (int32_t cellZ = -1; cellZ < sz; ++cellZ) {
      for (int32_t cellX = -1; cellX < sx; ++cellX) {
        float corners[8];
        bool hasSolid = false;
        bool hasAir = false;

        for (size_t ci = 0; ci < cornerOffsets.size(); ++ci) {
          const int32_t sxCoord = cellX + cornerOffsets[ci][0];
          const int32_t syCoord = cellY + cornerOffsets[ci][1];
          const int32_t szCoord = cellZ + cornerOffsets[ci][2];
          const float d = sampleAt(sxCoord, syCoord, szCoord);
          corners[ci] = d;
          hasSolid = hasSolid || (d <= 0.0f);
          hasAir = hasAir || (d > 0.0f);
        }

        if (!(hasSolid && hasAir)) continue;

        Vec3 accum = Vec3{0.0f, 0.0f, 0.0f};
        uint32_t edgeHits = 0u;

        for (const auto& edge : edgeCorners) {
          const int32_t a = edge[0];
          const int32_t b = edge[1];
          const float da = corners[static_cast<size_t>(a)];
          const float db = corners[static_cast<size_t>(b)];
          if ((da <= 0.0f) == (db <= 0.0f)) continue;

          const float denom = da - db;
          float t = 0.5f;
          if (std::fabs(denom) > 1e-20f) {
            t = da / denom;
          }
          t = std::clamp(t, 0.0f, 1.0f);

          const Vec3 pa = makeVec3(
              static_cast<float>(cellX + cornerOffsets[static_cast<size_t>(a)][0]),
              static_cast<float>(cellY + cornerOffsets[static_cast<size_t>(a)][1]),
              static_cast<float>(cellZ + cornerOffsets[static_cast<size_t>(a)][2]));
          const Vec3 pb = makeVec3(
              static_cast<float>(cellX + cornerOffsets[static_cast<size_t>(b)][0]),
              static_cast<float>(cellY + cornerOffsets[static_cast<size_t>(b)][1]),
              static_cast<float>(cellZ + cornerOffsets[static_cast<size_t>(b)][2]));
          accum = add(accum, add(pa, mul(sub(pb, pa), t)));
          ++edgeHits;
        }

        Vec3 pos = (edgeHits > 0u)
          ? mul(accum, 1.0f / static_cast<float>(edgeHits))
          : makeVec3(static_cast<float>(cellX) + 0.5f,
                     static_cast<float>(cellY) + 0.5f,
                     static_cast<float>(cellZ) + 0.5f);

        const float fx = std::clamp(pos.x - static_cast<float>(cellX), 0.0f, 1.0f);
        const float fy = std::clamp(pos.y - static_cast<float>(cellY), 0.0f, 1.0f);
        const float fz = std::clamp(pos.z - static_cast<float>(cellZ), 0.0f, 1.0f);
        Vec3 normal = normalize(interpolateGradient(cellX, cellY, cellZ, fx, fy, fz));

        if (vertexCount >= static_cast<uint32_t>(cfg.maxVertices)) {
          vertexCountOut = vertexCount;
          indexCountOut = indexCount;
          return false;
        }

        writeVertex(vertexOut, vertexCount, static_cast<uint32_t>(strideFloats), pos, normal);
        g_scratch.cellVertices[cellIndex(cellX + 1, cellY, cellZ + 1, cellXCount, cellZCount)] = vertexCount;
        ++vertexCount;
      }
    }
  }

  auto cellVertex = [&](int32_t x, int32_t y, int32_t z) -> uint32_t {
    if (x < -1 || x >= sx) return std::numeric_limits<uint32_t>::max();
    if (y < 0 || y >= sy) return std::numeric_limits<uint32_t>::max();
    if (z < -1 || z >= sz) return std::numeric_limits<uint32_t>::max();
    return g_scratch.cellVertices[cellIndex(x + 1, y, z + 1, cellXCount, cellZCount)];
  };

  auto emitQuad = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3) -> bool {
    if (i0 == std::numeric_limits<uint32_t>::max() ||
        i1 == std::numeric_limits<uint32_t>::max() ||
        i2 == std::numeric_limits<uint32_t>::max() ||
        i3 == std::numeric_limits<uint32_t>::max()) {
      return true;
    }

    const float* v0 = vertexOut + vertexOffset(i0, static_cast<uint32_t>(strideFloats));
    const float* v1 = vertexOut + vertexOffset(i1, static_cast<uint32_t>(strideFloats));
    const float* v2 = vertexOut + vertexOffset(i2, static_cast<uint32_t>(strideFloats));
    const float* v3 = vertexOut + vertexOffset(i3, static_cast<uint32_t>(strideFloats));

    const Vec3 p0{v0[0], v0[1], v0[2]};
    const Vec3 p1{v1[0], v1[1], v1[2]};
    const Vec3 p2{v2[0], v2[1], v2[2]};
    const Vec3 p3{v3[0], v3[1], v3[2]};
    Vec3 averageNormal = normalize(Vec3{
      v0[3] + v1[3] + v2[3] + v3[3],
      v0[4] + v1[4] + v2[4] + v3[4],
      v0[5] + v1[5] + v2[5] + v3[5],
    });

    Vec3 faceNormal = cross(sub(p1, p0), sub(p2, p0));
    if (lengthSquared(faceNormal) <= 1e-20f) {
      return true;
    }
    if (dot(faceNormal, averageNormal) < 0.0f) {
      std::swap(i1, i3);
    }

    if (indexCount + 6u > static_cast<uint32_t>(cfg.maxIndices)) {
      return false;
    }

    indexOut[indexCount++] = i0;
    indexOut[indexCount++] = i1;
    indexOut[indexCount++] = i2;
    indexOut[indexCount++] = i0;
    indexOut[indexCount++] = i2;
    indexOut[indexCount++] = i3;
    return true;
  };

  // Build quads from sign-changing lattice edges. The negative X/Z ghost halo
  // lets the chunk own its border stitching without relying on neighbor data.
  for (int32_t y = 1; y < sy; ++y) {
    for (int32_t z = 0; z < sz; ++z) {
      for (int32_t x = 0; x < sx; ++x) {
        const float d0 = sampleAt(x, y, z);
        const float d1 = sampleAt(x + 1, y, z);
        if ((d0 <= 0.0f) == (d1 <= 0.0f)) continue;

        const uint32_t c0 = cellVertex(x, y - 1, z - 1);
        const uint32_t c1 = cellVertex(x, y - 1, z);
        const uint32_t c2 = cellVertex(x, y, z);
        const uint32_t c3 = cellVertex(x, y, z - 1);
        if (!emitQuad(c0, c1, c2, c3)) {
          vertexCountOut = vertexCount;
          indexCountOut = indexCount;
          return false;
        }
      }
    }
  }

  for (int32_t y = 1; y < sy; ++y) {
    for (int32_t z = 0; z < sz; ++z) {
      for (int32_t x = 0; x < sx; ++x) {
        const float d0 = sampleAt(x, y, z);
        const float d1 = sampleAt(x, y + 1, z);
        if ((d0 <= 0.0f) == (d1 <= 0.0f)) continue;

        const uint32_t c0 = cellVertex(x - 1, y, z - 1);
        const uint32_t c1 = cellVertex(x, y, z - 1);
        const uint32_t c2 = cellVertex(x, y, z);
        const uint32_t c3 = cellVertex(x - 1, y, z);
        if (!emitQuad(c0, c1, c2, c3)) {
          vertexCountOut = vertexCount;
          indexCountOut = indexCount;
          return false;
        }
      }
    }
  }

  for (int32_t y = 1; y < sy; ++y) {
    for (int32_t z = 0; z < sz; ++z) {
      for (int32_t x = 0; x < sx; ++x) {
        const float d0 = sampleAt(x, y, z);
        const float d1 = sampleAt(x, y, z + 1);
        if ((d0 <= 0.0f) == (d1 <= 0.0f)) continue;

        const uint32_t c0 = cellVertex(x - 1, y - 1, z);
        const uint32_t c1 = cellVertex(x, y - 1, z);
        const uint32_t c2 = cellVertex(x, y, z);
        const uint32_t c3 = cellVertex(x - 1, y, z);
        if (!emitQuad(c0, c1, c2, c3)) {
          vertexCountOut = vertexCount;
          indexCountOut = indexCount;
          return false;
        }
      }
    }
  }

  vertexCountOut = vertexCount;
  indexCountOut = indexCount;
  return true;
}

} // namespace voxel::mesh
