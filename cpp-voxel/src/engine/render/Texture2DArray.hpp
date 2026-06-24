#pragma once

#include "gl_core.hpp"
#include <cstdint>
#include <cmath>

namespace voxel {

/// OpenGL 2D texture array for block textures.
class Texture2DArray {
public:
  Texture2DArray(int32_t width, int32_t height, int32_t layers);
  ~Texture2DArray();

  Texture2DArray(const Texture2DArray&) = delete;
  Texture2DArray& operator=(const Texture2DArray&) = delete;
  Texture2DArray(Texture2DArray&&) noexcept;
  Texture2DArray& operator=(Texture2DArray&&) noexcept;

  void uploadLayer(int32_t layer, const uint8_t* rgba, int32_t w, int32_t h);
  void generateMipmaps();
  void bind(uint32_t unit) const;
  [[nodiscard]] auto texture() const -> uint32_t { return m_texture; }

private:
  uint32_t m_texture = 0;
  int32_t m_width, m_height, m_layers;
};

} // namespace voxel
