#pragma once

#include <cmath>
#include <cstdint>

namespace voxel {

/// Utility: world coordinate → chunk coordinate.
inline auto worldToChunk(float coord, int32_t size) -> int32_t {
  return static_cast<int32_t>(std::floor(coord / static_cast<float>(size)));
}

/// Utility: positive modulo.
inline auto mod(int32_t value, int32_t size) -> int32_t {
  int32_t r = value % size;
  return r < 0 ? r + size : r;
}

} // namespace voxel
