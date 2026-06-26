#pragma once

#include <glm/glm.hpp>
#include <array>

namespace terrain {

/// Frustum for view-frustum culling of chunks and entities.
/// Stores 6 planes (left, right, bottom, top, near, far) as normalized vec4.
class Frustum {
public:
  /// Extract frustum planes from a view-projection matrix.
  void extractFrom(const glm::mat4& viewProjection);

  /// Test if an AABB is inside (or intersecting) the frustum.
  /// Returns true if the AABB is at least partially inside.
  [[nodiscard]] auto intersectsAABB(float minX, float minY, float minZ,
                                    float maxX, float maxY, float maxZ) const -> bool;

  /// Raw access to the 6 planes (each a vec4: x,y,z = normal, w = distance).
  [[nodiscard]] auto planes() const -> const std::array<glm::vec4, 6>& { return m_planes; }

private:
  std::array<glm::vec4, 6> m_planes{};
};

} // namespace terrain
