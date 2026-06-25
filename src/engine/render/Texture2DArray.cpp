#include "Texture2DArray.hpp"

namespace voxel {

bool Texture2DArray::supportsHighBitDepthTextures() {
  // GL_RGBA16 is core in OpenGL 3.0+. We target 4.6, so it is always
  // available. This check exists as a hook for future platforms or
  // configurations where 16-bit textures may not be supported.
  return true;
}

Texture2DArray::Texture2DArray(int32_t width, int32_t height, int32_t layers)
  : m_width(width), m_height(height), m_layers(layers),
    m_highBitDepth(supportsHighBitDepthTextures()) {
  // Use tight pixel packing for optimal memory transfer.
  // Safe for both 8-bit (4 bytes/pixel) and 16-bit (8 bytes/pixel) formats.
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);

  gl::GenTextures(1, &m_texture);
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);

  int32_t levels = 1; // only base level — NEAREST filtering used, mipmaps not needed
  GLenum internalFormat = m_highBitDepth ? GL_RGBA16 : GL_RGBA8;
  gl::TexStorage3D(GL_TEXTURE_2D_ARRAY, levels, internalFormat, width, height, layers);

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
    m_height(other.m_height), m_layers(other.m_layers),
    m_highBitDepth(other.m_highBitDepth) {
  other.m_texture = 0;
}

Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept {
  if (this != &other) {
    if (m_texture) gl::DeleteTextures(1, &m_texture);
    m_texture = other.m_texture;
    m_width = other.m_width;
    m_height = other.m_height;
    m_layers = other.m_layers;
    m_highBitDepth = other.m_highBitDepth;
    other.m_texture = 0;
  }
  return *this;
}

void Texture2DArray::uploadLayer(int32_t layer, const uint16_t* rgba, int32_t w, int32_t h) {
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
  gl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, w, h, 1, GL_RGBA, GL_UNSIGNED_SHORT, rgba);
}

void Texture2DArray::uploadLayer8(int32_t layer, const uint8_t* rgba, int32_t w, int32_t h) {
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
  gl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture2DArray::uploadAllLayers(const uint16_t* rgba, int32_t w, int32_t h, int32_t count) {
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
  // Single call uploads all layers at once — far less driver overhead than
  // N individual uploadLayer calls.
  gl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, w, h, count, GL_RGBA, GL_UNSIGNED_SHORT, rgba);
}

void Texture2DArray::uploadAllLayers8(const uint8_t* rgba, int32_t w, int32_t h, int32_t count) {
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
  gl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, w, h, count, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture2DArray::bind(uint32_t unit) const {
  gl::ActiveTexture(GL_TEXTURE0 + unit);
  gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
}

} // namespace voxel
