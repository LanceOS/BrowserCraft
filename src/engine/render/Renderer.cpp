#include "Renderer.hpp"
#include "shaders/ShaderSources.hpp"
#include "world/daynight/DayNightCycle.hpp"
#include "engine/assets/AssetManager.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>
namespace voxel {

Renderer::Renderer(GLFWwindow* window, BlockRegistry& blocks, const GameConfig& config,
                   ChunkMeshAllocator& meshAllocator)
  : m_window(window), m_config(config),
    m_chunkShader(shaders::chunkVertex, shaders::chunkFragment),
    m_skyShader(shaders::skyVertex, shaders::skyFragment),
    m_cameraUbo(0, CAMERA_BLOCK_FLOATS * sizeof(float)),
    m_timeUbo(2, TIME_BLOCK_FLOATS * sizeof(float)),
    m_textures(16, 16, std::max(1, AssetManager::get().getTextureLayerCount())),
    m_meshAllocator(meshAllocator),
    m_indirectBatcher([&]{
        int32_t poolCap = (config.renderDistance * 2 + 1) * (config.renderDistance * 2 + 1) + 8;
        return std::make_unique<IndirectBatcher>(poolCap);
    }()),
    m_chunkSyncer(m_meshAllocator, *m_indirectBatcher, config),
    m_drawDispatcher(m_chunkShader, m_skyShader, m_textures, *m_indirectBatcher, m_masterVao, m_skyVao)
{
  (void)blocks;

  m_chunkShader.bindUniformBlock("CameraBlock", 0);
  m_chunkShader.bindUniformBlock("TimeBlock", 2);
  m_skyShader.bindUniformBlock("CameraBlock", 0);
  m_skyShader.bindUniformBlock("TimeBlock", 2);

  // Create sky fullscreen triangle
  gl::GenVertexArrays(1, &m_skyVao);
  gl::GenBuffers(1, &m_skyVbo);
  gl::BindVertexArray(m_skyVao);
  gl::BindBuffer(GL_ARRAY_BUFFER, m_skyVbo);
  float skyVerts[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  gl::BufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  gl::BindVertexArray(0);
  gl::BindBuffer(GL_ARRAY_BUFFER, 0);

  gl::Enable(GL_DEPTH_TEST);
  gl::Disable(GL_CULL_FACE);
  // Blending is toggled per-pass in DrawDispatcher: off for opaque, on for transparent.
  gl::Disable(GL_BLEND);
  gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  gl::GenVertexArrays(1, &m_masterVao);
  gl::BindVertexArray(m_masterVao);
  gl::BindBuffer(GL_ARRAY_BUFFER, m_meshAllocator.vboBuffer());
  gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_meshAllocator.iboBuffer());

  size_t strideBytes = m_config.vertexStrideFloats * sizeof(float);
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideBytes, reinterpret_cast<void*>(0));
  gl::EnableVertexAttribArray(1);
  gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, strideBytes, reinterpret_cast<void*>(12));
  gl::EnableVertexAttribArray(2);
  gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideBytes, reinterpret_cast<void*>(24));
  gl::EnableVertexAttribArray(3);
  gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, strideBytes, reinterpret_cast<void*>(32));
  gl::EnableVertexAttribArray(4);
  gl::VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, strideBytes, reinterpret_cast<void*>(36));
  gl::BindVertexArray(0);

  seedTextureArray();
  updateFramebufferSize();
}

Renderer::~Renderer() {
  dispose();
}

auto Renderer::updateFramebufferSize() -> float {
  glfwGetFramebufferSize(m_window, &m_fbWidth, &m_fbHeight);
  m_fbWidth = std::max(1, m_fbWidth);
  m_fbHeight = std::max(1, m_fbHeight);
  return static_cast<float>(m_fbWidth) / static_cast<float>(m_fbHeight);
}

void Renderer::render(World& world, const CameraView& camera,
                       float timeSeconds, float daylightFactor,
                       float ambientIntensity, float normalizedTimeOfDay) {
  m_chunkSyncer.sync(world);
  m_frustum.extractFrom(camera.viewProjectionMatrix);

  gl::Viewport(0, 0, m_fbWidth, m_fbHeight);

  float skyR = 0.08f + 0.5f * daylightFactor;
  float skyG = 0.1f + 0.64f * daylightFactor;
  float skyB = 0.16f + 0.74f * daylightFactor;
  gl::ClearColor(skyR, skyG, skyB, 1.0f);
  gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  uploadCameraBlock(camera, timeSeconds, daylightFactor, skyR, skyG, skyB);

  // Upload time block
  {
    float dayFac = daylightFactor;
    glm::vec3 sunDir = daynight::computeSunDirection(timeSeconds);
    glm::vec3 sunCol = daynight::computeSunColor(timeSeconds);
    float sunIntensity = dayFac; // sun contribution scales with daylight

    // std140 layout (80 bytes = 20 floats):
    //   0: u_timeElapsed
    //   4: u_daylight
    //   8: u_sunIntensity
    //  12: u_ambientIntensity
    //  16: u_sunDir.x
    //  20: u_sunDir.y
    //  24: u_sunDir.z
    //  28: padding
    //  32: u_timeOfDay
    //  36: padding
    //  40: padding
    //  44: padding
    //  48: u_sunColor.r
    //  52: u_sunColor.g
    //  56: u_sunColor.b
    //  60: padding
    //  64: u_pad
    //  68: padding
    //  72: padding
    //  76: padding
    std::array<float, TIME_BLOCK_FLOATS> timeData{};
    timeData[0] = timeSeconds;          // u_timeElapsed
    timeData[1] = dayFac;               // u_daylight (0=night, 1=day)
    timeData[2] = sunIntensity;         // u_sunIntensity
    timeData[3] = ambientIntensity;     // u_ambientIntensity
    timeData[4] = sunDir.x;             // u_sunDir.x
    timeData[5] = sunDir.y;             // u_sunDir.y
    timeData[6] = sunDir.z;             // u_sunDir.z
    timeData[8] = normalizedTimeOfDay;   // u_timeOfDay (0-1)
    timeData[12] = sunCol.r;            // u_sunColor.r
    timeData[13] = sunCol.g;            // u_sunColor.g
    timeData[14] = sunCol.b;            // u_sunColor.b
    timeData[16] = 0.0f;                // u_pad
    m_timeUbo.upload(timeData.data(), sizeof(timeData));
  }

  m_drawDispatcher.renderSky();
  m_drawDispatcher.renderChunks(m_frustum);
}

void Renderer::dispose() {
  if (m_skyVao) gl::DeleteVertexArrays(1, &m_skyVao);
  if (m_skyVbo) gl::DeleteBuffers(1, &m_skyVbo);
  m_skyVao = 0;
  m_skyVbo = 0;

  if (m_masterVao) gl::DeleteVertexArrays(1, &m_masterVao);
  m_masterVao = 0;
  m_indirectBatcher.reset();
}

// ---- Private ----

void Renderer::uploadCameraBlock(const CameraView& camera, float timeSeconds,
                                  float daylightFactor, float skyR, float skyG, float skyB) {
  auto* cb = m_cameraBlock.data();

  // u_proj (mat4) at [0..15]
  std::memcpy(cb, &camera.projectionMatrix[0], 16 * sizeof(float));
  // u_view (mat4) at [16..31]
  std::memcpy(cb + 16, &camera.viewMatrix[0], 16 * sizeof(float));
  // u_projView (mat4) at [32..47]
  std::memcpy(cb + 32, &camera.viewProjectionMatrix[0], 16 * sizeof(float));
  // u_invProjView (mat4) at [48..63]
  std::memcpy(cb + 48, &camera.inverseViewProjectionMatrix[0], 16 * sizeof(float));
  // u_camTime (vec4) at [64..67]
  cb[64] = camera.position.x;
  cb[65] = camera.position.y;
  cb[66] = camera.position.z;
  cb[67] = timeSeconds;
  // u_fogColor (vec4) at [68..71]
  cb[68] = skyR * (0.55f + 0.45f * daylightFactor);
  cb[69] = skyG * (0.55f + 0.45f * daylightFactor);
  cb[70] = skyB * (0.6f + 0.4f * daylightFactor);
  cb[71] = static_cast<float>(m_config.renderDistance) * m_config.chunkSize * 1.8f;
  // u_camRight (vec4) at [72..75]
  cb[72] = camera.right.x;
  cb[73] = camera.right.y;
  cb[74] = camera.right.z;
  cb[75] = 0.0f;
  // u_camUp (vec4) at [76..79]
  cb[76] = camera.up.x;
  cb[77] = camera.up.y;
  cb[78] = camera.up.z;
  cb[79] = 0.0f;

  m_cameraUbo.upload(cb, CAMERA_BLOCK_FLOATS * sizeof(float));
}

void Renderer::seedTextureArray() {
  int32_t layers = AssetManager::get().getTextureLayerCount();
  if (layers == 0) return;

  const auto& pixelData = AssetManager::get().getTextureData();

  if (m_textures.isHighBitDepth()) {
    // Single upload for all layers — far less driver overhead than N calls.
    m_textures.uploadAllLayers(pixelData.data(), 16, 16, layers);
  } else {
    // Fallback: convert all layers to 8-bit at once, then single upload.
    const size_t totalPixels = static_cast<size_t>(layers) * 16 * 16 * 4;
    std::vector<uint8_t> fallbackData(totalPixels);
    const uint16_t* src = pixelData.data();
    for (size_t i = 0; i < totalPixels; ++i) {
      fallbackData[i] = static_cast<uint8_t>(src[i] >> 8);
    }
    m_textures.uploadAllLayers8(fallbackData.data(), 16, 16, layers);
  }
}

} // namespace voxel
