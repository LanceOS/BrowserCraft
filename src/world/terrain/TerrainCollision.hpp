#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace terrain {

struct TerrainTriangle {
  glm::vec3 v0;
  glm::vec3 v1;
  glm::vec3 v2;
  glm::vec3 normal;
};

struct CollisionContact {
  glm::vec3 normal{0.0f}; // Points from triangle to AABB (direction to push AABB out)
  float depth = 0.0f;     // Penetration depth
};

// Compute the Minimum Translation Vector (MTV) to push an AABB out of a triangle
bool collideAABBTriangle(const glm::vec3& boxCenter, const glm::vec3& boxHalfSize,
                         const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                         CollisionContact& contact);

struct TerrainBVHNode {
  glm::vec3 boundsMin;
  glm::vec3 boundsMax;
  int32_t leftChild = -1;  // -1 if leaf
  int32_t rightChild = -1; // -1 if leaf
  int32_t triangleStart = 0;
  int32_t triangleCount = 0;
};

class TerrainChunkCollision {
public:
  TerrainChunkCollision() = default;
  ~TerrainChunkCollision() = default;

  // Build the BVH from a list of triangles. Runs on worker threads.
  void build(std::vector<TerrainTriangle> triangles);

  // Check if an AABB intersects the terrain mesh.
  bool intersectsAABB(const glm::vec3& boxMin, const glm::vec3& boxMax) const;

  // Gather all triangles that potentially intersect the AABB using the BVH.
  void getTrianglesIntersectingAABB(const glm::vec3& boxMin, const glm::vec3& boxMax,
                                    std::vector<TerrainTriangle>& outTriangles) const;

  // Cast a ray against the terrain mesh, returning the closest hit.
  bool raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
               glm::vec3& outHitPos, glm::vec3& outHitNormal, float& outHitDist) const;

  [[nodiscard]] const std::vector<TerrainTriangle>& getTriangles() const { return m_triangles; }
  [[nodiscard]] const std::vector<TerrainBVHNode>& getNodes() const { return m_nodes; }
  [[nodiscard]] bool empty() const { return m_triangles.empty(); }

private:
  void buildRecursive(int32_t nodeIdx, int32_t start, int32_t count);
  bool intersectsAABBRecursive(int32_t nodeIdx, const glm::vec3& boxMin, const glm::vec3& boxMax) const;
  void getTrianglesIntersectingAABBRecursive(int32_t nodeIdx, const glm::vec3& boxMin, const glm::vec3& boxMax,
                                             std::vector<TerrainTriangle>& outTriangles) const;
  bool raycastRecursive(int32_t nodeIdx, const glm::vec3& origin, const glm::vec3& direction,
                        float& inOutMaxDist, glm::vec3& outHitPos, glm::vec3& outHitNormal) const;

  std::vector<TerrainTriangle> m_triangles;
  std::vector<TerrainBVHNode> m_nodes;
};

} // namespace terrain
