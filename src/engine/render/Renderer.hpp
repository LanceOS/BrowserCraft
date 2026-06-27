#pragma once

#include "ShaderProgram.hpp"
#include "UniformBuffer.hpp"
#include "Texture2DArray.hpp"
#include "ChunkMeshAllocator.hpp"
#include "IndirectBatcher.hpp"
#include "ChunkSyncer.hpp"
#include "DrawDispatcher.hpp"
#include "CameraView.hpp"
#include "../math/Frustum.hpp"
#include "../math/AABB.hpp"
#include "../../world/World.hpp"
#include "../../engine/core/Config.hpp"
#include <GLFW/glfw3.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace terrain {

/// Main OpenGL renderer for the terrain engine.
/// Manages shaders, uniforms, textures, and per-chunk meshes.
class Renderer {
public:
  static constexpr int32_t VERTEX_STRIDE_FLOATS = 10;
  static constexpr int32_t CAMERA_BLOCK_FLOATS = 80;
  static constexpr int32_t TIME_BLOCK_FLOATS = 20;

  Renderer(GLFWwindow* window, const GameConfig& config,
           ChunkMeshAllocator& meshAllocator);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  /// Render a frame.
  void render(World& world, const CameraView& camera, float timeSeconds, float daylightFactor,
              float ambientIntensity, float normalizedTimeOfDay);

  /// Resize to match the framebuffer.
  auto updateFramebufferSize() -> float;

  void dispose();

  /// Accessors for GPU buffer targets (used by ChunkWorker for direct mesh output).
  [[nodiscard]] auto vboPtr() const -> float* { return m_meshAllocator.vboPtr(); }
  [[nodiscard]] auto vboBytes() const -> size_t { return m_meshAllocator.vboCapacityBytes(); }
  [[nodiscard]] auto iboPtr() const -> uint32_t* { return m_meshAllocator.iboPtr(); }
  [[nodiscard]] auto iboBytes() const -> size_t { return m_meshAllocator.iboCapacityBytes(); }

private:
  void uploadCameraBlock(const CameraView& camera, float timeSeconds,
                         float daylightFactor, float skyR, float skyG, float skyB);
  void seedTextureArray();

  GLFWwindow* m_window;
  const GameConfig& m_config;

  ShaderProgram m_terrainShader;
  ShaderProgram m_skyShader;
  UniformBuffer m_cameraUbo;
  UniformBuffer m_timeUbo;
  Texture2DArray m_textures;
  ChunkMeshAllocator& m_meshAllocator;

  Frustum m_frustum;
  std::array<float, CAMERA_BLOCK_FLOATS> m_cameraBlock{};

  // Sky fullscreen triangle
  uint32_t m_skyVao = 0;
  uint32_t m_skyVbo = 0;

  // Master VAO for chunk rendering
  uint32_t m_masterVao = 0;
  std::unique_ptr<IndirectBatcher> m_indirectBatcher;

  ChunkSyncer m_chunkSyncer;
  DrawDispatcher m_drawDispatcher;

  int32_t m_fbWidth = 1;
  int32_t m_fbHeight = 1;
};

} // namespace terrain
