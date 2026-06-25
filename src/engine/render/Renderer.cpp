#include "Renderer.hpp"
#include "shaders/ShaderSources.hpp"
#include "world/daynight/DayNightCycle.hpp"
#include "engine/assets/AssetManager.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
namespace voxel {

Renderer::Renderer(GLFWwindow* window, BlockRegistry& blocks, const GameConfig& config)
  : m_window(window), m_config(config),
    m_chunkShader(shaders::chunkVertex, shaders::chunkFragment),
    m_skyShader(shaders::skyVertex, shaders::skyFragment),
    m_cameraUbo(0, CAMERA_BLOCK_FLOATS * sizeof(float)),
    m_timeUbo(2, TIME_BLOCK_FLOATS * sizeof(float)),
    m_textures(16, 16, std::max(1, AssetManager::get().getTextureLayerCount()))
{
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
  gl::Enable(GL_BLEND);
  gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Initialize master persistently mapped buffers
  int32_t rd = m_config.renderDistance;
  int32_t poolCap = (rd * 2 + 1) * (rd * 2 + 1) + 8;
  size_t vboSize = static_cast<size_t>(poolCap) * m_config.maxVertsPerChunk * m_config.vertexStrideFloats * sizeof(float);
  size_t iboSize = static_cast<size_t>(poolCap) * m_config.maxIndicesPerChunk * sizeof(uint32_t);

  m_masterVbo = std::make_unique<PersistentBuffer>(vboSize, GL_ARRAY_BUFFER);
  m_masterIbo = std::make_unique<PersistentBuffer>(iboSize, GL_ELEMENT_ARRAY_BUFFER);
  m_indirectBatcher = std::make_unique<IndirectBatcher>(poolCap);

  gl::GenVertexArrays(1, &m_masterVao);
  gl::BindVertexArray(m_masterVao);
  gl::BindBuffer(GL_ARRAY_BUFFER, m_masterVbo->buffer());
  gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_masterIbo->buffer());

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
                       float timeSeconds, float daylightFactor) {
  syncChunks(world);

  gl::Viewport(0, 0, m_fbWidth, m_fbHeight);

  float skyR = 0.08f + 0.5f * daylightFactor;
  float skyG = 0.1f + 0.64f * daylightFactor;
  float skyB = 0.16f + 0.74f * daylightFactor;
  gl::ClearColor(skyR, skyG, skyB, 1.0f);
  gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  uploadCameraBlock(camera, timeSeconds, daylightFactor, skyR, skyG, skyB);

  // Upload time block
  {
    float sunAngle = daynight::computeSunAngle(timeSeconds);
    float dayFac = daylightFactor;
    float timeData[8] = {
      timeSeconds,           // u_timeElapsed
      sunAngle,              // u_sunAngle
      dayFac,                // u_daylight
      dayFac * 15.0f,        // u_lightLevel
      std::sin(sunAngle),    // u_sunDir.x
      std::cos(sunAngle),    // u_sunDir.y
      0.3f,                  // u_sunDir.z
      0.0f                   // u_pad
    };
    m_timeUbo.upload(timeData, sizeof(timeData));
  }

  renderSky();

  // ---- Render chunks ----
  m_chunkShader.use();
  m_textures.bind(0);
  gl::Uniform1i(m_chunkShader.uniform("u_blockTextures"), 0);

  m_indirectBatcher->dispatchCulling(&camera.viewProjectionMatrix[0][0]);

  // Re-bind chunk shader after dispatchCulling switches to compute program
  m_chunkShader.use();

  gl::BindVertexArray(m_masterVao);

  // Pass 1: opaque only
  gl::Uniform1i(m_chunkShader.uniform("u_opaquePass"), 1);
  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
  m_indirectBatcher->drawIndirect();

  // Pass 2: transparent only
  gl::Uniform1i(m_chunkShader.uniform("u_opaquePass"), 0);
  gl::DepthMask(GL_FALSE);
  gl::DepthFunc(GL_LEQUAL);
  m_indirectBatcher->drawIndirect();

  gl::BindVertexArray(0);

  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
}

void Renderer::dispose() {
  if (m_skyVao) gl::DeleteVertexArrays(1, &m_skyVao);
  if (m_skyVbo) gl::DeleteBuffers(1, &m_skyVbo);
  m_skyVao = 0;
  m_skyVbo = 0;

  if (m_masterVao) gl::DeleteVertexArrays(1, &m_masterVao);
  m_masterVao = 0;
  m_masterVbo.reset();
  m_masterIbo.reset();
  m_indirectBatcher.reset();
}

// ---- Private ----

void Renderer::syncChunks(World& world) {
  std::vector<bool> activeSlots(m_indirectBatcher->capacity(), false);

  // Upload new/updated meshes
  world.forEachEntry([&](int64_t key, Chunk& chunk) {
    auto slot = world.getChunkSlot(chunk);
    // @see notes/renderer-slot-index-bounds.md
    if (slot.slotIndex < 0 || slot.slotIndex >= static_cast<int32_t>(m_indirectBatcher->capacity())) {
      return;
    }

    activeSlots[slot.slotIndex] = true;

    if (chunk.state == ChunkState::MeshReady) {
      if (chunk.indexCount > 0 && chunk.vertexCount > 0) {
        const int32_t stride = m_config.vertexStrideFloats;

        size_t vboOffset = static_cast<size_t>(slot.slotIndex) * m_config.maxVertsPerChunk * stride * sizeof(float);
        size_t iboOffset = static_cast<size_t>(slot.slotIndex) * m_config.maxIndicesPerChunk * sizeof(uint32_t);
        int32_t baseVertex = slot.slotIndex * m_config.maxVertsPerChunk;

        // Directly copy vertices and indices to the persistently mapped GPU memory
        std::memcpy(static_cast<uint8_t*>(m_masterVbo->mappedPtr()) + vboOffset,
                    slot.vertices, static_cast<size_t>(chunk.vertexCount) * stride * sizeof(float));
        std::memcpy(static_cast<uint8_t*>(m_masterIbo->mappedPtr()) + iboOffset,
                    slot.indices, chunk.indexCount * sizeof(uint32_t));

        ChunkCullData cullData{};
        cullData.min[0] = static_cast<float>(chunk.chunkX * m_config.chunkSize);
        cullData.min[1] = 0.0f;
        cullData.min[2] = static_cast<float>(chunk.chunkZ * m_config.chunkSize);
        cullData.min[3] = 1.0f;
        cullData.max[0] = cullData.min[0] + static_cast<float>(m_config.chunkSize);
        cullData.max[1] = static_cast<float>(m_config.worldHeight);
        cullData.max[2] = cullData.min[2] + static_cast<float>(m_config.chunkSize);
        cullData.max[3] = 1.0f;
        cullData.indexCount = chunk.indexCount;
        cullData.firstIndex = static_cast<uint32_t>(iboOffset / sizeof(uint32_t));
        cullData.baseVertex = static_cast<uint32_t>(baseVertex);
        cullData.slotIndex = static_cast<uint32_t>(slot.slotIndex);
        m_indirectBatcher->updateChunkData(slot.slotIndex, cullData);
      } else {
        ChunkCullData emptyData{};
        m_indirectBatcher->updateChunkData(slot.slotIndex, emptyData);
      }
      world.markUploaded(chunk);
    }
  });

  // Zero out slots that are no longer active
  for (uint32_t i = 0; i < m_indirectBatcher->capacity(); ++i) {
    if (!activeSlots[i]) {
       ChunkCullData emptyData{};
       m_indirectBatcher->updateChunkData(i, emptyData);
    }
  }
}

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

void Renderer::renderSky() {
  gl::DepthMask(GL_FALSE);
  gl::Disable(GL_DEPTH_TEST);
  gl::Disable(GL_CULL_FACE);
  m_skyShader.use();
  gl::BindVertexArray(m_skyVao);
  gl::DrawArrays(GL_TRIANGLES, 0, 3);
  gl::BindVertexArray(0);
  gl::Enable(GL_DEPTH_TEST);
  gl::DepthMask(GL_TRUE);
}

void Renderer::seedTextureArray() {
  int32_t layers = AssetManager::get().getTextureLayerCount();
  if (layers == 0) return;

  const auto& pixelData = AssetManager::get().getTextureData();
  size_t bytesPerLayer = 16 * 16 * 4;

  for (int32_t layer = 0; layer < layers; ++layer) {
    const uint8_t* layerData = pixelData.data() + (layer * bytesPerLayer);
    m_textures.uploadLayer(layer, layerData, 16, 16);
  }
  m_textures.generateMipmaps();
}

} // namespace voxel
