#pragma once

#include "gl_core.hpp"
#include <cstdint>
#include <cmath>

namespace terrain {

/// OpenGL 2D texture array for block textures.
/// Supports 16-bit per channel (64-bit per pixel) RGBA textures
/// with automatic fallback to 8-bit if the platform does not support it.
class Texture2DArray {
public:
  Texture2DArray(int32_t width, int32_t height, int32_t layers);
  ~Texture2DArray();

  Texture2DArray(const Texture2DArray&) = delete;
  Texture2DArray& operator=(const Texture2DArray&) = delete;
  Texture2DArray(Texture2DArray&&) noexcept;
  Texture2DArray& operator=(Texture2DArray&&) noexcept;

  /// Upload a single 16-bit-per-channel RGBA texture layer.
  void uploadLayer(int32_t layer, const uint16_t* rgba, int32_t w, int32_t h);
  /// Upload a single 8-bit-per-channel RGBA texture layer (fallback path).
  void uploadLayer8(int32_t layer, const uint8_t* rgba, int32_t w, int32_t h);
  /// Upload all layers at once with a single TexSubImage3D call (16-bit).
  void uploadAllLayers(const uint16_t* rgba, int32_t w, int32_t h, int32_t count);
  /// Upload all layers at once with a single TexSubImage3D call (8-bit fallback).
  void uploadAllLayers8(const uint8_t* rgba, int32_t w, int32_t h, int32_t count);
  void bind(uint32_t unit) const;
  [[nodiscard]] auto texture() const -> uint32_t { return m_texture; }
  [[nodiscard]] auto isHighBitDepth() const -> bool { return m_highBitDepth; }

  /// Check whether the runtime supports high-bit-depth (16-bit) textures.
  static bool supportsHighBitDepthTextures();

private:
  uint32_t m_texture = 0;
  int32_t m_width, m_height, m_layers;
  bool m_highBitDepth = true;
};

} // namespace terrain
