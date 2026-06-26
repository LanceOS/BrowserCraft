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

  // Cast a ray against the terrain mesh, returning the closest hit.
  bool raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
               glm::vec3& outHitPos, glm::vec3& outHitNormal, float& outHitDist) const;

  [[nodiscard]] const std::vector<TerrainTriangle>& getTriangles() const { return m_triangles; }
  [[nodiscard]] const std::vector<TerrainBVHNode>& getNodes() const { return m_nodes; }
  [[nodiscard]] bool empty() const { return m_triangles.empty(); }

private:
  void buildRecursive(int32_t nodeIdx, int32_t start, int32_t count);
  bool intersectsAABBRecursive(int32_t nodeIdx, const glm::vec3& boxMin, const glm::vec3& boxMax) const;
  bool raycastRecursive(int32_t nodeIdx, const glm::vec3& origin, const glm::vec3& direction,
                        float& inOutMaxDist, glm::vec3& outHitPos, glm::vec3& outHitNormal) const;

  std::vector<TerrainTriangle> m_triangles;
  std::vector<TerrainBVHNode> m_nodes;
};

} // namespace terrain
