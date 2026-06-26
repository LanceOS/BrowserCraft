#include "TerrainCollision.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxel {

// ============================================================================
// Intersection Helper Functions
// ============================================================================

// Ray-AABB intersection (Slab method)
bool rayAABBIntersect(const glm::vec3& origin, const glm::vec3& dir,
                      const glm::vec3& boxMin, const glm::vec3& boxMax,
                      float& tMin, float& tMax) {
  tMin = 0.0f;
  tMax = std::numeric_limits<float>::infinity();
  for (int i = 0; i < 3; ++i) {
    if (std::abs(dir[i]) < 1e-6f) {
      if (origin[i] < boxMin[i] || origin[i] > boxMax[i]) return false;
    } else {
      float invDir = 1.0f / dir[i];
      float t1 = (boxMin[i] - origin[i]) * invDir;
      float t2 = (boxMax[i] - origin[i]) * invDir;
      if (t1 > t2) std::swap(t1, t2);
      tMin = std::max(tMin, t1);
      tMax = std::min(tMax, t2);
      if (tMin > tMax) return false;
    }
  }
  return true;
}

// Ray-Triangle intersection (Möller–Trumbore algorithm)
bool rayTriangleIntersect(const glm::vec3& origin, const glm::vec3& dir,
                          const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                          float& t, glm::vec3& normal) {
  glm::vec3 edge1 = v1 - v0;
  glm::vec3 edge2 = v2 - v0;
  glm::vec3 h = glm::cross(dir, edge2);
  float a = glm::dot(edge1, h);
  if (a > -1e-6f && a < 1e-6f) return false; // Ray is parallel to the triangle.

  float f = 1.0f / a;
  glm::vec3 s = origin - v0;
  float u = f * glm::dot(s, h);
  if (u < 0.0f || u > 1.0f) return false;

  glm::vec3 q = glm::cross(s, edge1);
  float v = f * glm::dot(dir, q);
  if (v < 0.0f || u + v > 1.0f) return false;

  float tempT = f * glm::dot(edge2, q);
  if (tempT > 1e-4f) { // Ray intersection
    t = tempT;
    normal = glm::normalize(glm::cross(edge1, edge2));
    return true;
  }
  return false;
}

// AABB-Triangle intersection (Separating Axis Theorem)
bool intersectsAABBTriangle(const glm::vec3& boxCenter, const glm::vec3& boxHalfSize,
                            const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
  // Translate vertices relative to box center
  glm::vec3 u0 = v0 - boxCenter;
  glm::vec3 u1 = v1 - boxCenter;
  glm::vec3 u2 = v2 - boxCenter;

  // Test 1: Coordinate axes (AABB face normals)
  // X axis
  float minX = std::min({u0.x, u1.x, u2.x});
  float maxX = std::max({u0.x, u1.x, u2.x});
  if (minX > boxHalfSize.x || maxX < -boxHalfSize.x) return false;

  // Y axis
  float minY = std::min({u0.y, u1.y, u2.y});
  float maxY = std::max({u0.y, u1.y, u2.y});
  if (minY > boxHalfSize.y || maxY < -boxHalfSize.y) return false;

  // Z axis
  float minZ = std::min({u0.z, u1.z, u2.z});
  float maxZ = std::max({u0.z, u1.z, u2.z});
  if (minZ > boxHalfSize.z || maxZ < -boxHalfSize.z) return false;

  // Test 2: Triangle normal
  glm::vec3 edge0 = u1 - u0;
  glm::vec3 edge1 = u2 - u1;
  glm::vec3 normal = glm::cross(edge0, edge1);
  float r = boxHalfSize.x * std::abs(normal.x) +
            boxHalfSize.y * std::abs(normal.y) +
            boxHalfSize.z * std::abs(normal.z);
  float d = glm::dot(normal, u0);
  if (std::abs(d) > r) return false;

  // Test 3: 9 edge cross products
  glm::vec3 edge2 = u0 - u2;

  auto testAxis = [](float p0, float p1, float p2, float boxRadius) -> bool {
    float minP = std::min({p0, p1, p2});
    float maxP = std::max({p0, p1, p2});
    return minP <= boxRadius && maxP >= -boxRadius;
  };

  // Axis a00 = e0 x u_x = (0, -e0.z, e0.y)
  {
    float r_proj = boxHalfSize.y * std::abs(edge0.z) + boxHalfSize.z * std::abs(edge0.y);
    float proj0 = -edge0.z * u0.y + edge0.y * u0.z;
    float proj1 = -edge0.z * u1.y + edge0.y * u1.z;
    float proj2 = -edge0.z * u2.y + edge0.y * u2.z;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a01 = e1 x u_x = (0, -edge1.z, edge1.y)
  {
    float r_proj = boxHalfSize.y * std::abs(edge1.z) + boxHalfSize.z * std::abs(edge1.y);
    float proj0 = -edge1.z * u0.y + edge1.y * u0.z;
    float proj1 = -edge1.z * u1.y + edge1.y * u1.z;
    float proj2 = -edge1.z * u2.y + edge1.y * u2.z;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a02 = e2 x u_x = (0, -edge2.z, edge2.y)
  {
    float r_proj = boxHalfSize.y * std::abs(edge2.z) + boxHalfSize.z * std::abs(edge2.y);
    float proj0 = -edge2.z * u0.y + edge2.y * u0.z;
    float proj1 = -edge2.z * u1.y + edge2.y * u1.z;
    float proj2 = -edge2.z * u2.y + edge2.y * u2.z;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a10 = e0 x u_y = (edge0.z, 0, -edge0.x)
  {
    float r_proj = boxHalfSize.x * std::abs(edge0.z) + boxHalfSize.z * std::abs(edge0.x);
    float proj0 = edge0.z * u0.x - edge0.x * u0.z;
    float proj1 = edge0.z * u1.x - edge0.x * u1.z;
    float proj2 = edge0.z * u2.x - edge0.x * u2.z;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a11 = e1 x u_y = (edge1.z, 0, -edge1.x)
  {
    float r_proj = boxHalfSize.x * std::abs(edge1.z) + boxHalfSize.z * std::abs(edge1.x);
    float proj0 = edge1.z * u0.x - edge1.x * u0.z;
    float proj1 = edge1.z * u1.x - edge1.x * u1.z;
    float proj2 = edge1.z * u2.x - edge1.x * u2.z;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a12 = e2 x u_y = (edge2.z, 0, -edge2.x)
  {
    float r_proj = boxHalfSize.x * std::abs(edge2.z) + boxHalfSize.z * std::abs(edge2.x);
    float proj0 = edge2.z * u0.x - edge2.x * u0.z;
    float proj1 = edge2.z * u1.x - edge2.x * u1.z;
    float proj2 = edge2.z * u2.x - edge2.x * u2.z;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a20 = e0 x u_z = (-edge0.y, edge0.x, 0)
  {
    float r_proj = boxHalfSize.x * std::abs(edge0.y) + boxHalfSize.y * std::abs(edge0.x);
    float proj0 = -edge0.y * u0.x + edge0.x * u0.y;
    float proj1 = -edge0.y * u1.x + edge0.x * u1.y;
    float proj2 = -edge0.y * u2.x + edge0.x * u2.y;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a21 = e1 x u_z = (-edge1.y, edge1.x, 0)
  {
    float r_proj = boxHalfSize.x * std::abs(edge1.y) + boxHalfSize.y * std::abs(edge1.x);
    float proj0 = -edge1.y * u0.x + edge1.x * u0.y;
    float proj1 = -edge1.y * u1.x + edge1.x * u1.y;
    float proj2 = -edge1.y * u2.x + edge1.x * u2.y;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  // Axis a22 = e2 x u_z = (-edge2.y, edge2.x, 0)
  {
    float r_proj = boxHalfSize.x * std::abs(edge2.y) + boxHalfSize.y * std::abs(edge2.x);
    float proj0 = -edge2.y * u0.x + edge2.x * u0.y;
    float proj1 = -edge2.y * u1.x + edge2.x * u1.y;
    float proj2 = -edge2.y * u2.x + edge2.x * u2.y;
    if (!testAxis(proj0, proj1, proj2, r_proj)) return false;
  }

  return true;
}

static inline bool aabbOverlap(const glm::vec3& minA, const glm::vec3& maxA,
                               const glm::vec3& minB, const glm::vec3& maxB) {
  return minA.x <= maxB.x && maxA.x >= minB.x &&
         minA.y <= maxB.y && maxA.y >= minB.y &&
         minA.z <= maxB.z && maxA.z >= minB.z;
}

// ============================================================================
// TerrainChunkCollision Method Implementations
// ============================================================================

void TerrainChunkCollision::build(std::vector<TerrainTriangle> triangles) {
  m_triangles = std::move(triangles);
  m_nodes.clear();
  if (m_triangles.empty()) return;

  // Reserve worst-case nodes
  m_nodes.reserve(m_triangles.size() * 2);

  // Allocate root
  m_nodes.push_back(TerrainBVHNode{});
  buildRecursive(0, 0, static_cast<int32_t>(m_triangles.size()));
}

void TerrainChunkCollision::buildRecursive(int32_t nodeIdx, int32_t start, int32_t count) {
  glm::vec3 boxMin(std::numeric_limits<float>::infinity());
  glm::vec3 boxMax(-std::numeric_limits<float>::infinity());
  for (int32_t i = 0; i < count; ++i) {
    const auto& tri = m_triangles[static_cast<size_t>(start + i)];
    boxMin = glm::min(boxMin, glm::min(tri.v0, glm::min(tri.v1, tri.v2)));
    boxMax = glm::max(boxMax, glm::max(tri.v0, glm::max(tri.v1, tri.v2)));
  }

  m_nodes[static_cast<size_t>(nodeIdx)].boundsMin = boxMin;
  m_nodes[static_cast<size_t>(nodeIdx)].boundsMax = boxMax;

  if (count <= 4) {
    m_nodes[static_cast<size_t>(nodeIdx)].leftChild = -1;
    m_nodes[static_cast<size_t>(nodeIdx)].rightChild = -1;
    m_nodes[static_cast<size_t>(nodeIdx)].triangleStart = start;
    m_nodes[static_cast<size_t>(nodeIdx)].triangleCount = count;
    return;
  }

  glm::vec3 extents = boxMax - boxMin;
  int axis = 0;
  if (extents.y > extents.x && extents.y > extents.z) axis = 1;
  else if (extents.z > extents.x && extents.z > extents.y) axis = 2;

  std::sort(m_triangles.begin() + start, m_triangles.begin() + start + count,
            [axis](const TerrainTriangle& a, const TerrainTriangle& b) {
              float centA = (a.v0[axis] + a.v1[axis] + a.v2[axis]) / 3.0f;
              float centB = (b.v0[axis] + b.v1[axis] + b.v2[axis]) / 3.0f;
              return centA < centB;
            });

  int32_t leftCount = count / 2;
  int32_t rightCount = count - leftCount;

  int32_t leftIdx = static_cast<int32_t>(m_nodes.size());
  m_nodes.push_back(TerrainBVHNode{});
  int32_t rightIdx = static_cast<int32_t>(m_nodes.size());
  m_nodes.push_back(TerrainBVHNode{});

  m_nodes[static_cast<size_t>(nodeIdx)].leftChild = leftIdx;
  m_nodes[static_cast<size_t>(nodeIdx)].rightChild = rightIdx;
  m_nodes[static_cast<size_t>(nodeIdx)].triangleStart = 0;
  m_nodes[static_cast<size_t>(nodeIdx)].triangleCount = 0;

  buildRecursive(leftIdx, start, leftCount);
  buildRecursive(rightIdx, start + leftCount, rightCount);
}

bool TerrainChunkCollision::intersectsAABB(const glm::vec3& boxMin, const glm::vec3& boxMax) const {
  if (m_triangles.empty()) return false;
  return intersectsAABBRecursive(0, boxMin, boxMax);
}

bool TerrainChunkCollision::intersectsAABBRecursive(int32_t nodeIdx, const glm::vec3& boxMin, const glm::vec3& boxMax) const {
  const auto& node = m_nodes[static_cast<size_t>(nodeIdx)];
  if (!aabbOverlap(node.boundsMin, node.boundsMax, boxMin, boxMax)) {
    return false;
  }

  if (node.leftChild == -1) {
    glm::vec3 boxCenter = 0.5f * (boxMin + boxMax);
    glm::vec3 boxHalfSize = 0.5f * (boxMax - boxMin);
    for (int32_t i = 0; i < node.triangleCount; ++i) {
      const auto& tri = m_triangles[static_cast<size_t>(node.triangleStart + i)];
      if (intersectsAABBTriangle(boxCenter, boxHalfSize, tri.v0, tri.v1, tri.v2)) {
        return true;
      }
    }
    return false;
  }

  return intersectsAABBRecursive(node.leftChild, boxMin, boxMax) ||
         intersectsAABBRecursive(node.rightChild, boxMin, boxMax);
}

bool TerrainChunkCollision::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                    glm::vec3& outHitPos, glm::vec3& outHitNormal, float& outHitDist) const {
  if (m_triangles.empty()) return false;

  float closestDist = maxDistance;
  glm::vec3 closestNormal(0.0f);
  glm::vec3 closestPos(0.0f);

  bool hit = raycastRecursive(0, origin, direction, closestDist, closestPos, closestNormal);
  if (hit) {
    outHitPos = closestPos;
    outHitNormal = closestNormal;
    outHitDist = closestDist;
    return true;
  }
  return false;
}

bool TerrainChunkCollision::raycastRecursive(int32_t nodeIdx, const glm::vec3& origin, const glm::vec3& direction,
                                             float& inOutMaxDist, glm::vec3& outHitPos, glm::vec3& outHitNormal) const {
  const auto& node = m_nodes[static_cast<size_t>(nodeIdx)];
  float tMin = 0.0f, tMax = 0.0f;
  if (!rayAABBIntersect(origin, direction, node.boundsMin, node.boundsMax, tMin, tMax)) {
    return false;
  }
  if (tMin > inOutMaxDist) {
    return false;
  }

  if (node.leftChild == -1) {
    bool hitAny = false;
    for (int32_t i = 0; i < node.triangleCount; ++i) {
      const auto& tri = m_triangles[static_cast<size_t>(node.triangleStart + i)];
      float t = 0.0f;
      glm::vec3 norm(0.0f);
      if (rayTriangleIntersect(origin, direction, tri.v0, tri.v1, tri.v2, t, norm)) {
        if (t < inOutMaxDist) {
          inOutMaxDist = t;
          outHitPos = origin + direction * t;
          outHitNormal = norm;
          hitAny = true;
        }
      }
    }
    return hitAny;
  }

  float tMinLeft = 0.0f, tMaxLeft = 0.0f;
  float tMinRight = 0.0f, tMaxRight = 0.0f;
  bool hitLeft = rayAABBIntersect(origin, direction, m_nodes[static_cast<size_t>(node.leftChild)].boundsMin, m_nodes[static_cast<size_t>(node.leftChild)].boundsMax, tMinLeft, tMaxLeft);
  bool hitRight = rayAABBIntersect(origin, direction, m_nodes[static_cast<size_t>(node.rightChild)].boundsMin, m_nodes[static_cast<size_t>(node.rightChild)].boundsMax, tMinRight, tMaxRight);

  bool hitAny = false;
  if (hitLeft && hitRight) {
    if (tMinLeft < tMinRight) {
      if (raycastRecursive(node.leftChild, origin, direction, inOutMaxDist, outHitPos, outHitNormal)) hitAny = true;
      if (tMinRight < inOutMaxDist) {
        if (raycastRecursive(node.rightChild, origin, direction, inOutMaxDist, outHitPos, outHitNormal)) hitAny = true;
      }
    } else {
      if (raycastRecursive(node.rightChild, origin, direction, inOutMaxDist, outHitPos, outHitNormal)) hitAny = true;
      if (tMinLeft < inOutMaxDist) {
        if (raycastRecursive(node.leftChild, origin, direction, inOutMaxDist, outHitPos, outHitNormal)) hitAny = true;
      }
    }
  } else if (hitLeft) {
    if (raycastRecursive(node.leftChild, origin, direction, inOutMaxDist, outHitPos, outHitNormal)) hitAny = true;
  } else if (hitRight) {
    if (raycastRecursive(node.rightChild, origin, direction, inOutMaxDist, outHitPos, outHitNormal)) hitAny = true;
  }

  return hitAny;
}

} // namespace voxel
