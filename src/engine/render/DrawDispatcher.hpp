#pragma once

#include "ShaderProgram.hpp"
#include "Texture2DArray.hpp"
#include "CameraView.hpp"
#include "../math/Frustum.hpp"
#include "gl_core.hpp"
#include <cstdint>

namespace voxel {

class IndirectBatcher;

/// Manages the draw-pass logic for chunk rendering.
/// Owns sky rendering, indirect command building, and opaque/transparent draw passes.
class DrawDispatcher {
public:
  DrawDispatcher(ShaderProgram& chunkShader, ShaderProgram& skyShader,
                 Texture2DArray& textures, IndirectBatcher& indirectBatcher,
                 uint32_t& masterVao, uint32_t& skyVao);

  /// Render the sky fullscreen triangle.
  void renderSky();

  /// Render all chunks using indirect multidraw.
  /// After this call the caller should restore depth state.
  void renderChunks(const Frustum& frustum);

private:
  ShaderProgram& m_chunkShader;
  ShaderProgram& m_skyShader;
  Texture2DArray& m_textures;
  IndirectBatcher& m_indirectBatcher;
  uint32_t& m_masterVao;
  uint32_t& m_skyVao;
};

} // namespace voxel
