#include "DrawDispatcher.hpp"
#include "IndirectBatcher.hpp"
#include "gl_core.hpp"

namespace terrain {

DrawDispatcher::DrawDispatcher(ShaderProgram& terrainShader, ShaderProgram& skyShader,
                               Texture2DArray& textures, IndirectBatcher& indirectBatcher,
                               uint32_t& masterVao, uint32_t& skyVao)
  : m_terrainShader(terrainShader), m_skyShader(skyShader),
    m_textures(textures), m_indirectBatcher(indirectBatcher),
    m_masterVao(masterVao), m_skyVao(skyVao)
{}

void DrawDispatcher::renderSky() {
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

void DrawDispatcher::renderChunks(const Frustum& frustum) {
  m_textures.bind(0);
  m_indirectBatcher.buildIndirectCommands(frustum);

  gl::BindVertexArray(m_masterVao);
  gl::Enable(GL_CULL_FACE);
  ::glCullFace(GL_BACK);

  const uint32_t terrainOpaqueCount = m_indirectBatcher.terrainOpaqueCommandCount();
  const uint32_t terrainTransparentCount = m_indirectBatcher.terrainTransparentCommandCount();

  m_terrainShader.use();
  gl::Uniform1i(m_terrainShader.uniform("u_terrainTextures"), 0);

  // Terrain pass: opaque geometry first.
  if (terrainOpaqueCount > 0u) {
    gl::Disable(GL_BLEND);
    gl::Uniform1i(m_terrainShader.uniform("u_opaquePass"), 1);
    gl::DepthMask(GL_TRUE);
    gl::DepthFunc(GL_LESS);
    m_indirectBatcher.drawIndirect(terrainOpaqueCount, m_indirectBatcher.terrainOpaqueCommandBase());
  }

  // Terrain transparent pass, kept for future material effects.
  if (terrainTransparentCount > 0u) {
    gl::Enable(GL_BLEND);
    gl::Uniform1i(m_terrainShader.uniform("u_opaquePass"), 0);
    gl::DepthMask(GL_FALSE);
    gl::DepthFunc(GL_LEQUAL);
    m_indirectBatcher.drawIndirect(terrainTransparentCount, m_indirectBatcher.terrainTransparentCommandBase());
  }

  gl::BindVertexArray(0);
  gl::Disable(GL_CULL_FACE);
  gl::Disable(GL_BLEND);
  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
}

} // namespace terrain
