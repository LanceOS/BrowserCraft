#pragma once

#include "world/terrain/TerrainEditHistory.hpp"
#include <string>

namespace voxel {

/// Handles serialization, deserialization, and appending of TerrainEditHistory to/from a compact binary format.
class TerrainSaveData {
public:
  static constexpr uint32_t MAGIC = 0x54454454; // "TEDT" (Terrain Edit Data)
  static constexpr uint32_t CURRENT_VERSION = 1;

  /// Loads all terrain edits from the specified file path into the history.
  /// Returns true if successful (even if the file does not exist, which is treated as an empty history).
  static auto load(const std::string& filePath, TerrainEditHistory& history) -> bool;

  /// Saves the entire edit history to the specified file path (overwriting).
  static auto save(const std::string& filePath, const TerrainEditHistory& history) -> bool;

  /// Appends a single edit to the specified file path.
  static auto append(const std::string& filePath, const TerrainEdit& edit) -> bool;
};

} // namespace voxel
