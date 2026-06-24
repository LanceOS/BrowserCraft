#include "Renderer.hpp"
#include "shaders/ShaderSources.hpp"
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
    m_textures(16, 16, config.textureArrayLayers)
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
    float sunAngle = timeSeconds * 0.05f;
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

  m_frustum.extractFrom(camera.viewProjectionMatrix);

  struct VisibleChunk {
    std::string key;
    ChunkMesh* mesh;
    int32_t chunkX, chunkZ;
  };
  std::vector<VisibleChunk> visible;

  world.forEachEntry([&](const std::string& key, const Chunk& chunk) {
    auto it = m_meshes.find(key);
    if (it == m_meshes.end() || !it->second.valid()) return;

    auto box = AABB::fromChunk(
      chunk.chunkX, chunk.chunkZ,
      static_cast<float>(m_config.chunkSize),
      static_cast<float>(m_config.worldHeight),
      static_cast<float>(m_config.chunkSize));

    if (!m_frustum.intersectsAABB(box.minX, box.minY, box.minZ,
                                   box.maxX, box.maxY, box.maxZ)) return;

    visible.push_back({key, &it->second, chunk.chunkX, chunk.chunkZ});
  });

  // Sort far-to-near for correct transparent blending
  int32_t camCX = static_cast<int32_t>(std::floor(camera.position.x / m_config.chunkSize));
  int32_t camCZ = static_cast<int32_t>(std::floor(camera.position.z / m_config.chunkSize));
  std::sort(visible.begin(), visible.end(), [camCX, camCZ](const auto& a, const auto& b) {
    int32_t da = (a.chunkX - camCX) * (a.chunkX - camCX) + (a.chunkZ - camCZ) * (a.chunkZ - camCZ);
    int32_t db = (b.chunkX - camCX) * (b.chunkX - camCX) + (b.chunkZ - camCZ) * (b.chunkZ - camCZ);
    return db < da; // far first
  });

  // Pass 1: opaque only
  gl::Uniform1i(m_chunkShader.uniform("u_opaquePass"), 1);
  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
  for (auto& vc : visible) {
    gl::Uniform3f(m_chunkShader.uniform("u_chunkTranslation"),
                  static_cast<float>(vc.chunkX) * m_config.chunkSize, 0.0f,
                  static_cast<float>(vc.chunkZ) * m_config.chunkSize);
    vc.mesh->draw();
  }

  // Pass 2: transparent only
  gl::Uniform1i(m_chunkShader.uniform("u_opaquePass"), 0);
  gl::DepthMask(GL_FALSE);
  gl::DepthFunc(GL_LEQUAL);
  for (auto& vc : visible) {
    gl::Uniform3f(m_chunkShader.uniform("u_chunkTranslation"),
                  static_cast<float>(vc.chunkX) * m_config.chunkSize, 0.0f,
                  static_cast<float>(vc.chunkZ) * m_config.chunkSize);
    vc.mesh->draw();
  }

  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
}

void Renderer::dispose() {
  m_meshes.clear();
  if (m_skyVao) gl::DeleteVertexArrays(1, &m_skyVao);
  if (m_skyVbo) gl::DeleteBuffers(1, &m_skyVbo);
  m_skyVao = 0;
  m_skyVbo = 0;
}

// ---- Private ----

void Renderer::syncChunks(World& world) {
  // Remove meshes for chunks no longer in world
  for (auto it = m_meshes.begin(); it != m_meshes.end();) {
    if (!world.hasChunkKey(it->first)) {
      it = m_meshes.erase(it);
    } else {
      ++it;
    }
  }

  // Upload new/updated meshes
  world.forEachEntry([&](const std::string& key, Chunk& chunk) {
    if (chunk.state != ChunkState::MeshReady) return;
    if (chunk.indexCount == 0 || chunk.vertexCount == 0) {
      world.markUploaded(chunk);
      return;
    }

    auto& mesh = m_meshes[key];
    auto slot = world.getChunkSlot(chunk);
    const int32_t stride = m_config.vertexStrideFloats;
    mesh.upload(
      slot.vertices,
      static_cast<size_t>(chunk.vertexCount) * static_cast<size_t>(stride),
      slot.indices,
      chunk.indexCount,
      stride
    );
    world.markUploaded(chunk);
  });
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
  // Create placeholder solid-color textures for each layer.
  // In the full engine, TexturePipeline generates these from assets.
  int32_t layers = m_config.textureArrayLayers;
  std::vector<uint8_t> pixels(16 * 16 * 4);

  // Simple palette for different texture indices
  for (int32_t layer = 0; layer < layers; ++layer) {
    uint8_t r = static_cast<uint8_t>((layer * 37 + 80) % 256);
    uint8_t g = static_cast<uint8_t>((layer * 53 + 120) % 256);
    uint8_t b = static_cast<uint8_t>((layer * 71 + 160) % 256);
    for (int i = 0; i < 16 * 16; ++i) {
      pixels[i * 4 + 0] = r;
      pixels[i * 4 + 1] = g;
      pixels[i * 4 + 2] = b;
      pixels[i * 4 + 3] = 255;
    }
    m_textures.uploadLayer(layer, pixels.data(), 16, 16);
  }
  m_textures.generateMipmaps();
}

} // namespace voxel
