#pragma once

#include <cstdint>

namespace terrain::cmp {

/// Audio emitter component for the ECS.
struct AudioEmitter {
  float cooldown = 0.0f;
  float pitch = 1.0f;
  float volume = 1.0f;
};

} // namespace terrain::cmp
