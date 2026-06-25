#pragma once

#include "WorldMetadata.hpp"
#include <vector>
#include <string>
#include <filesystem>

namespace voxel {

/// Scans the saves directory for available worlds and provides sorted results.
/// Caches metadata for performance; call refresh() to rescan.
class WorldListService {
public:
  explicit WorldListService(std::filesystem::path saveDir);

  /// Rescan the saves directory and rebuild the world list.
  void refresh();

  /// Get the list of worlds (sorted most-recent-first).
  [[nodiscard]] auto worlds() const -> const std::vector<WorldMetadata>& { return m_worlds; }

  /// Get the number of available worlds.
  [[nodiscard]] auto count() const -> size_t { return m_worlds.size(); }

  /// Get a world by index.
  [[nodiscard]] auto get(size_t index) const -> const WorldMetadata* {
    if (index >= m_worlds.size()) return nullptr;
    return &m_worlds[index];
  }

  /// Find a world by slug.
  [[nodiscard]] auto findBySlug(std::string_view slug) const -> const WorldMetadata*;

  /// Check if any worlds exist.
  [[nodiscard]] auto empty() const -> bool { return m_worlds.empty(); }

private:
  std::filesystem::path m_saveDir;
  std::vector<WorldMetadata> m_worlds;
};

} // namespace voxel
