#include "TerrainCollision.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace terrain {

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

bool collideAABBTriangle(const glm::vec3& boxCenter, const glm::vec3& boxHalfSize,
                         const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                         CollisionContact& contact) {
  // Translate vertices relative to box center
  glm::vec3 u0 = v0 - boxCenter;
  glm::vec3 u1 = v1 - boxCenter;
  glm::vec3 u2 = v2 - boxCenter;

  glm::vec3 edge0 = u1 - u0;
  glm::vec3 edge1 = u2 - u1;
  glm::vec3 edge2 = u0 - u2;

  // Candidate axes (3 face normals + 1 triangle normal + 9 edge cross products)
  glm::vec3 axes[13];
  axes[0] = glm::vec3(1.0f, 0.0f, 0.0f);
  axes[1] = glm::vec3(0.0f, 1.0f, 0.0f);
  axes[2] = glm::vec3(0.0f, 0.0f, 1.0f);
  
  axes[3] = glm::cross(edge0, edge1);
  
  axes[4] = glm::vec3(0.0f, -edge0.z, edge0.y); // edge0 x u_x
  axes[5] = glm::vec3(0.0f, -edge1.z, edge1.y); // edge1 x u_x
  axes[6] = glm::vec3(0.0f, -edge2.z, edge2.y); // edge2 x u_x
  
  axes[7] = glm::vec3(edge0.z, 0.0f, -edge0.x); // edge0 x u_y
  axes[8] = glm::vec3(edge1.z, 0.0f, -edge1.x); // edge1 x u_y
  axes[9] = glm::vec3(edge2.z, 0.0f, -edge2.x); // edge2 x u_y
  
  axes[10] = glm::vec3(-edge0.y, edge0.x, 0.0f); // edge0 x u_z
  axes[11] = glm::vec3(-edge1.y, edge1.x, 0.0f); // edge1 x u_z
  axes[12] = glm::vec3(-edge2.y, edge2.x, 0.0f); // edge2 x u_z

  float minOverlap = std::numeric_limits<float>::infinity();
  glm::vec3 bestAxis(0.0f);

  for (int i = 0; i < 13; ++i) {
    glm::vec3 L = axes[i];
    float len2 = glm::dot(L, L);
    if (len2 < 1e-8f) continue; // Skip degenerate axes
    L /= std::sqrt(len2); // Normalize

    // Project AABB
    float r = boxHalfSize.x * std::abs(L.x) +
              boxHalfSize.y * std::abs(L.y) +
              boxHalfSize.z * std::abs(L.z);

    // Project Triangle
    float p0 = glm::dot(u0, L);
    float p1 = glm::dot(u1, L);
    float p2 = glm::dot(u2, L);

    float minP = std::min({p0, p1, p2});
    float maxP = std::max({p0, p1, p2});

    // Check overlap
    if (minP > r || maxP < -r) {
      return false; // Separating axis found, no intersection!
    }

    // Calculate overlap/push distance
    float shift1 = minP - r; // negative
    float shift2 = maxP + r; // positive
    float overlap = (std::abs(shift1) < std::abs(shift2)) ? shift1 : shift2;
    float overlapMagnitude = std::abs(overlap);

    if (overlapMagnitude < minOverlap) {
      minOverlap = overlapMagnitude;
      bestAxis = L * (overlap < 0.0f ? -1.0f : 1.0f);
    }
  }

  // If we got here, all axes overlap. Intersection exists!
  contact.normal = bestAxis; // Points from triangle to AABB
  contact.depth = minOverlap;
  return true;
}

static inline bool aabbOverlap(const glm::vec3& minA, const glm::vec3& maxA,
                               const glm::vec3& minB, const glm::vec3& maxB) {
  return minA.x <= maxB.x && maxA.x >= minB.x &&
         minA.y <= maxB.y && maxA.y >= minB.y &&
         minA.z <= maxB.z && maxA.z >= minB.z;
}

// ---------------------------------------------------------------------------
// Kasper Fauerby's Swept Ellipsoid (Sphere) vs Triangle Collision
// This algorithm treats the player as an ellipsoid, and transforms all math
// into "Ellipsoid Space" (eSpace) where the player becomes a unit sphere (radius 1).
// This drastically simplifies swept collision detection against arbitrary triangles.
// ---------------------------------------------------------------------------

// Solves the quadratic equation (at^2 + bt + c = 0) to find the earliest time of impact (t).
// Returns true if a valid root is found between 0.0 and maxR.
static bool getLowestRoot(float a, float b, float c, float maxR, float& root) {
  float determinant = b * b - 4.0f * a * c;
  if (determinant < 0.0f) return false;
  float sqrtD = std::sqrt(determinant);
  float r1 = (-b - sqrtD) / (2.0f * a);
  float r2 = (-b + sqrtD) / (2.0f * a);

  if (r1 > r2) std::swap(r1, r2);

  if (r1 > 0.0f && r1 < maxR) {
    root = r1;
    return true;
  }
  if (r2 > 0.0f && r2 < maxR) {
    root = r2;
    return true;
  }
  return false;
}

// Uses Barycentric Coordinates to determine if a point (p) lies inside the boundaries
// of a triangle (a, b, c). Used to verify if a sphere's collision point on the triangle's
// plane is actually within the triangle itself.
static bool checkPointInTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
  glm::vec3 v0 = c - a;
  glm::vec3 v1 = b - a;
  glm::vec3 v2 = p - a;

  float dot00 = glm::dot(v0, v0);
  float dot01 = glm::dot(v0, v1);
  float dot02 = glm::dot(v0, v2);
  float dot11 = glm::dot(v1, v1);
  float dot12 = glm::dot(v1, v2);

  float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
  float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
  float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

  return (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f);
}

// Core collision routine. Sweeps a unit sphere from eOrigin along eVel against a triangle.
// Updates contact.t with the earliest time of impact [0, 1] if a collision occurs.
void sweepSphereTriangle(const glm::vec3& eOrigin, const glm::vec3& eVel,
                         const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                         SweepContact& contact) 
{
  float t0, t1;
  bool embeddedInPlane = false;

  // Calculate the triangle's surface normal
  glm::vec3 e1 = p2 - p1;
  glm::vec3 e2 = p3 - p1;
  glm::vec3 normal = glm::normalize(glm::cross(e1, e2));

  float planeD = -glm::dot(normal, p1);
  float signedDistToPlane = glm::dot(eOrigin, normal) + planeD;
  float normalDotVel = glm::dot(normal, eVel);

  if (std::abs(normalDotVel) < 1e-5f) {
    if (std::abs(signedDistToPlane) >= 1.0f) {
      return;
    }
    embeddedInPlane = true;
    t0 = 0.0f;
    t1 = 1.0f;
  } else {
    t0 = (-1.0f - signedDistToPlane) / normalDotVel;
    t1 = ( 1.0f - signedDistToPlane) / normalDotVel;

    if (t0 > t1) std::swap(t0, t1);

    if (t0 > 1.0f || t1 < 0.0f) return;

    if (t0 < 0.0f) t0 = 0.0f;
    if (t1 > 1.0f) t1 = 1.0f;
  }

  // Early out: if the earliest intersection time with the plane is worse than
  // our best known collision time, this triangle cannot be the closest impact.
  if (t0 >= contact.t) return;

  bool foundCollision = false;
  float t = contact.t;
  glm::vec3 collisionPoint(0.0f);

  // 1. Test Plane Interior (Face Collision)
  // Does the sphere hit the face of the triangle?
  if (!embeddedInPlane) {
    // Calculate the exact point on the plane where the sphere touches it
    glm::vec3 planeIntersect = eOrigin - normal + t0 * eVel;
    if (checkPointInTriangle(planeIntersect, p1, p2, p3)) {
      foundCollision = true;
      t = t0;
      collisionPoint = planeIntersect;
    }
  }

  // 2. Test Vertices & Edges
  // If the sphere didn't hit the flat face of the triangle (e.g. it clipped the edge or point),
  // we sweep the sphere against the vertices (Ray vs Sphere) and edges (Ray vs Cylinder).
  if (!foundCollision) {
    float velocitySq = glm::dot(eVel, eVel);
    float a = velocitySq;

    // Vertices
    glm::vec3 pts[3] = {p1, p2, p3};
    for (int i = 0; i < 3; ++i) {
      glm::vec3 baseToV = eOrigin - pts[i];
      float b = 2.0f * glm::dot(eVel, baseToV);
      float c = glm::dot(baseToV, baseToV) - 1.0f;
      float newT;
      if (getLowestRoot(a, b, c, t, newT)) {
        t = newT;
        foundCollision = true;
        collisionPoint = pts[i];
      }
    }

    // Edges
    glm::vec3 edges[3] = {p2 - p1, p3 - p2, p1 - p3};
    for (int i = 0; i < 3; ++i) {
      glm::vec3 edge = edges[i];
      glm::vec3 baseToV = eOrigin - pts[i];
      
      float edgeSqLen = glm::dot(edge, edge);
      float edgeDotVel = glm::dot(edge, eVel);
      float edgeDotBaseToV = glm::dot(edge, baseToV);
      
      float aEdge = edgeSqLen * velocitySq - edgeDotVel * edgeDotVel;
      float bEdge = edgeSqLen * (2.0f * glm::dot(eVel, baseToV)) - 2.0f * edgeDotVel * edgeDotBaseToV;
      float cEdge = edgeSqLen * (glm::dot(baseToV, baseToV) - 1.0f) - edgeDotBaseToV * edgeDotBaseToV;

      float newT;
      if (getLowestRoot(aEdge, bEdge, cEdge, t, newT)) {
        float f = (edgeDotVel * newT - edgeDotBaseToV) / edgeSqLen;
        if (f >= 0.0f && f <= 1.0f) {
          t = newT;
          foundCollision = true;
          collisionPoint = pts[i] + f * edge;
        }
      }
    }
  }

  if (foundCollision) {
    contact.t = t;
    contact.hitPoint = collisionPoint;
    contact.hit = true;
    contact.normal = glm::normalize(eOrigin + eVel * t - collisionPoint);
  }
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

void TerrainChunkCollision::getTrianglesIntersectingAABB(
    const glm::vec3& boxMin, const glm::vec3& boxMax,
    std::vector<TerrainTriangle>& outTriangles) const {
  if (m_triangles.empty()) return;
  getTrianglesIntersectingAABBRecursive(0, boxMin, boxMax, outTriangles);
}

void TerrainChunkCollision::getTrianglesIntersectingAABBRecursive(
    int32_t nodeIdx, const glm::vec3& boxMin, const glm::vec3& boxMax,
    std::vector<TerrainTriangle>& outTriangles) const {
  const auto& node = m_nodes[static_cast<size_t>(nodeIdx)];
  if (!aabbOverlap(node.boundsMin, node.boundsMax, boxMin, boxMax)) {
    return;
  }

  if (node.leftChild == -1) {
    for (int32_t i = 0; i < node.triangleCount; ++i) {
      const auto& tri = m_triangles[static_cast<size_t>(node.triangleStart + i)];
      outTriangles.push_back(tri);
    }
    return;
  }

  getTrianglesIntersectingAABBRecursive(node.leftChild, boxMin, boxMax, outTriangles);
  getTrianglesIntersectingAABBRecursive(node.rightChild, boxMin, boxMax, outTriangles);
}

} // namespace terrain
