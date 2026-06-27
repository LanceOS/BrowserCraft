#pragma once

#include <cstdint>
#include <string>

namespace terrain {

/// A lightweight representation of a saved world for menus and persistence.
struct WorldEntry {
  std::string name;
  std::string slug;
  uint32_t seed = 0;
  int32_t gameMode = 0; // 0 = Survival, 1 = Creative
  int64_t lastPlayedTimestamp = 0;
};

} // namespace terrain
