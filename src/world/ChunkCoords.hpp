#pragma once

#include <cmath>
#include <cstdint>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

/// Utility: world coordinate → chunk coordinate.
inline auto worldToChunk(float coord, int32_t size) -> int32_t {
  return static_cast<int32_t>(std::floor(coord / static_cast<float>(size)));
}

/// Utility: integer coordinate → chunk coordinate using floor division.
inline auto floorToChunk(int32_t coord, int32_t size) -> int32_t {
  if (coord >= 0) return coord / size;
  return -1 - ((-coord - 1) / size);
}

/// Utility: positive modulo.
inline auto mod(int32_t value, int32_t size) -> int32_t {
  int32_t r = value % size;
  return r < 0 ? r + size : r;
}

} // namespace voxel
