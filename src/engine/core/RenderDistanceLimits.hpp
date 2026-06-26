#pragma once

#include <cstdint>

namespace voxel {

/// Shared clamp range for render-distance settings across UI, save, and session code.
inline constexpr int32_t MIN_RENDER_DISTANCE = 2;
inline constexpr int32_t MAX_RENDER_DISTANCE = 24;

} // namespace voxel
