#pragma once

#include <cstdint>

// @deprecated Legacy terrain-world code retained during the render-only migration to triangle meshes.
namespace terrain::mesh {

/// Configuration for the Surface Nets terrain mesher.
struct SurfaceNetsConfig {
  int32_t sizeX = 16;
  int32_t sizeY = 256;
  int32_t sizeZ = 16;
  int32_t maxVertices = 50000;
  int32_t maxIndices = 100000;
  int32_t strideFloats = 10;
  float originX = 0.0f;
  float originY = 0.0f;
  float originZ = 0.0f;
};

/// C-style density callback used by the Surface Nets mesher.
struct DensitySampler {
  void* userData = nullptr;
  float (*sample)(void* userData, float worldX, float worldY, float worldZ) = nullptr;

  [[nodiscard]] auto valid() const -> bool { return sample != nullptr; }

  [[nodiscard]] auto operator()(float worldX, float worldY, float worldZ) const -> float {
    return sample(userData, worldX, worldY, worldZ);
  }
};

/// Extract a Surface Nets mesh from the scalar density field.
///
/// The output layout matches the standard chunk mesher vertex format:
/// position, normal, uv, texLayer, packedLight.
[[nodiscard]] auto surfaceNetsMesh(
    const SurfaceNetsConfig& cfg,
    const DensitySampler& density,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut) -> bool;

} // namespace terrain::mesh
