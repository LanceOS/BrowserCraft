#pragma once

#include "ShaderProgram.hpp"
#include "UniformBuffer.hpp"
#include "Texture2DArray.hpp"
#include "PersistentBuffer.hpp"
#include "IndirectBatcher.hpp"
#include "CameraView.hpp"
#include "../math/Frustum.hpp"
#include "../math/AABB.hpp"
#include "../../world/World.hpp"
#include "../../world/BlockRegistry.hpp"
#include "../../engine/core/Config.hpp"
#include <GLFW/glfw3.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace voxel {

/// Main OpenGL renderer for the voxel engine.
/// Manages shaders, uniforms, textures, and per-chunk meshes.
class Renderer {
public:
  static constexpr int32_t VERTEX_STRIDE_FLOATS = 10;
  static constexpr int32_t CAMERA_BLOCK_FLOATS = 80;
  static constexpr int32_t TIME_BLOCK_FLOATS = 8;

  Renderer(GLFWwindow* window, BlockRegistry& blocks, const GameConfig& config);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  /// Render a frame.
  void render(World& world, const CameraView& camera, float timeSeconds, float daylightFactor);

  /// Resize to match the framebuffer.
  auto updateFramebufferSize() -> float;

  void dispose();

private:
  void syncChunks(World& world);
  void uploadCameraBlock(const CameraView& camera, float timeSeconds,
                         float daylightFactor, float skyR, float skyG, float skyB);
  void renderSky();
  void seedTextureArray();

  GLFWwindow* m_window;
  const GameConfig& m_config;

  ShaderProgram m_chunkShader;
  ShaderProgram m_skyShader;
  UniformBuffer m_cameraUbo;
  UniformBuffer m_timeUbo;
  Texture2DArray m_textures;

  Frustum m_frustum;
  std::array<float, CAMERA_BLOCK_FLOATS> m_cameraBlock{};

  // Sky fullscreen triangle
  uint32_t m_skyVao = 0;
  uint32_t m_skyVbo = 0;

  // Master VAO and persistently mapped buffers for all chunks
  uint32_t m_masterVao = 0;
  std::unique_ptr<PersistentBuffer> m_masterVbo;
  std::unique_ptr<PersistentBuffer> m_masterIbo;
  std::unique_ptr<IndirectBatcher> m_indirectBatcher;

  int32_t m_fbWidth = 1;
  int32_t m_fbHeight = 1;
};

} // namespace voxel
