#include "DrawDispatcher.hpp"
#include "IndirectBatcher.hpp"
#include "gl_core.hpp"

namespace voxel {

DrawDispatcher::DrawDispatcher(ShaderProgram& chunkShader, ShaderProgram& skyShader,
                               Texture2DArray& textures, IndirectBatcher& indirectBatcher,
                               uint32_t& masterVao, uint32_t& skyVao)
  : m_chunkShader(chunkShader), m_skyShader(skyShader),
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

void DrawDispatcher::renderChunks(bool hasTransparentChunks,
                                   const float* viewProjectionMatrix) {
  m_chunkShader.use();
  m_textures.bind(0);
  gl::Uniform1i(m_chunkShader.uniform("u_blockTextures"), 0);

  m_indirectBatcher.buildIndirectCommands(viewProjectionMatrix);

  gl::BindVertexArray(m_masterVao);

  // Pass 1: opaque only
  gl::Uniform1i(m_chunkShader.uniform("u_opaquePass"), 1);
  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
  m_indirectBatcher.drawIndirect();

  // Pass 2: transparent only (skip if no transparent chunks are loaded)
  if (hasTransparentChunks) {
    gl::Uniform1i(m_chunkShader.uniform("u_opaquePass"), 0);
    gl::DepthMask(GL_FALSE);
    gl::DepthFunc(GL_LEQUAL);
    m_indirectBatcher.drawIndirect();
  }

  gl::BindVertexArray(0);

  gl::DepthMask(GL_TRUE);
  gl::DepthFunc(GL_LESS);
}

} // namespace voxel
