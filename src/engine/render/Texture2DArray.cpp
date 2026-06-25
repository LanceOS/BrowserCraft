#include "Texture2DArray.hpp"

namespace voxel {

Texture2DArray::Texture2DArray(int32_t width, int32_t height, int32_t layers)
  : m_width(width), m_height(height), m_layers(layers) {
  gl::GenTextures(1, &m_texture);
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);

  int32_t levels = static_cast<int32_t>(std::log2(std::max(width, height))) + 1;
  gl::TexStorage3D(GL_TEXTURE_2D_ARRAY, levels, GL_RGBA8, width, height, layers);

  gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

Texture2DArray::~Texture2DArray() {
  if (m_texture) gl::DeleteTextures(1, &m_texture);
}

Texture2DArray::Texture2DArray(Texture2DArray&& other) noexcept
  : m_texture(other.m_texture), m_width(other.m_width),
    m_height(other.m_height), m_layers(other.m_layers) {
  other.m_texture = 0;
}

Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept {
  if (this != &other) {
    if (m_texture) gl::DeleteTextures(1, &m_texture);
    m_texture = other.m_texture;
    m_width = other.m_width;
    m_height = other.m_height;
    m_layers = other.m_layers;
    other.m_texture = 0;
  }
  return *this;
}

void Texture2DArray::uploadLayer(int32_t layer, const uint8_t* rgba, int32_t w, int32_t h) {
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
  gl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture2DArray::generateMipmaps() {
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
  gl::GenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

void Texture2DArray::bind(uint32_t unit) const {
  gl::ActiveTexture(GL_TEXTURE0 + unit);
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
}

} // namespace voxel
