#pragma once

#include <cstdint>

namespace voxel::cmp {

/// Audio emitter component for the ECS.
struct AudioEmitter {
  float cooldown = 0.0f;
  float pitch = 1.0f;
  float volume = 1.0f;
};

} // namespace voxel::cmp
