#include "Frustum.hpp"

namespace terrain {

void Frustum::extractFrom(const glm::mat4& vp) {
  // Left plane
  m_planes[0] = glm::vec4(
    vp[0][3] + vp[0][0],
    vp[1][3] + vp[1][0],
    vp[2][3] + vp[2][0],
    vp[3][3] + vp[3][0]
  );
  // Right plane
  m_planes[1] = glm::vec4(
    vp[0][3] - vp[0][0],
    vp[1][3] - vp[1][0],
    vp[2][3] - vp[2][0],
    vp[3][3] - vp[3][0]
  );
  // Bottom plane
  m_planes[2] = glm::vec4(
    vp[0][3] + vp[0][1],
    vp[1][3] + vp[1][1],
    vp[2][3] + vp[2][1],
    vp[3][3] + vp[3][1]
  );
  // Top plane
  m_planes[3] = glm::vec4(
    vp[0][3] - vp[0][1],
    vp[1][3] - vp[1][1],
    vp[2][3] - vp[2][1],
    vp[3][3] - vp[3][1]
  );
  // Near plane
  m_planes[4] = glm::vec4(
    vp[0][3] + vp[0][2],
    vp[1][3] + vp[1][2],
    vp[2][3] + vp[2][2],
    vp[3][3] + vp[3][2]
  );
  // Far plane
  m_planes[5] = glm::vec4(
    vp[0][3] - vp[0][2],
    vp[1][3] - vp[1][2],
    vp[2][3] - vp[2][2],
    vp[3][3] - vp[3][2]
  );

  // Normalize all 6 planes
  for (auto& plane : m_planes) {
    float len = glm::length(glm::vec3(plane));
    if (len > 0.0f) {
      plane /= len;
    }
  }
}

auto Frustum::intersectsAABB(float minX, float minY, float minZ,
                             float maxX, float maxY, float maxZ) const -> bool {
  for (const auto& plane : m_planes) {
    // Pick the corner of the AABB furthest in the direction of the plane normal
    float pvx = plane.x > 0.0f ? maxX : minX;
    float pvy = plane.y > 0.0f ? maxY : minY;
    float pvz = plane.z > 0.0f ? maxZ : minZ;

    // If the furthest corner is behind the plane, the AABB is outside
    if (plane.x * pvx + plane.y * pvy + plane.z * pvz + plane.w < 0.0f) {
      return false;
    }
  }
  return true;
}

} // namespace terrain
