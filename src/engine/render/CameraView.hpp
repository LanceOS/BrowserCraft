#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace voxel {

/// Camera view data passed to the renderer.
struct CameraView {
  glm::vec3 position{0.0f, 64.0f, 0.0f};
  glm::vec3 forward{0.0f, 0.0f, -1.0f};
  glm::vec3 right{1.0f, 0.0f, 0.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  glm::mat4 projectionMatrix{1.0f};
  glm::mat4 viewMatrix{1.0f};
  glm::mat4 viewProjectionMatrix{1.0f};
  glm::mat4 inverseViewProjectionMatrix{1.0f};
};

} // namespace voxel
