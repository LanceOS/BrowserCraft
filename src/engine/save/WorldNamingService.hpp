#pragma once

#include <string>
#include <string_view>
#include <filesystem>

namespace voxel {

/// Centralized world name validation, slug generation, and collision detection.
class WorldNamingService {
public:
  explicit WorldNamingService(std::filesystem::path saveDir);

  /// Check whether a display name (or slug) is already taken by an existing world.
  [[nodiscard]] auto isNameTaken(std::string_view displayName) const -> bool;

  /// Check whether a specific slug is already used by an existing world.
  [[nodiscard]] auto isSlugTaken(std::string_view slug) const -> bool;

  /// Generate a filesystem-safe slug from a display name.
  [[nodiscard]] static auto generateSlug(std::string_view displayName) -> std::string;

  /// If the display name maps to an existing slug, return an alternate slug
  /// (e.g., "my_world_2"). Otherwise return the base slug.
  [[nodiscard]] auto nextAvailableSlug(std::string_view displayName) const -> std::string;

  /// Validate a display name. Returns an error string if invalid, or empty if valid.
  [[nodiscard]] static auto validateName(std::string_view displayName) -> std::string;

private:
  std::filesystem::path m_saveDir;
};

} // namespace voxel
