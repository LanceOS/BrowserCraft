#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <ctime>
#include <fstream>
#include <filesystem>

namespace terrain {

/// Persistent metadata for a saved world.
/// Stored as a binary file (<saveDir>/<slug>/world.meta) with magic + version
/// for forward compatibility.
struct WorldMetadata {
  static constexpr uint32_t MAGIC = 0x574F524C;    // "WORL"
  static constexpr uint32_t CURRENT_VERSION = 1;

  uint32_t magic = MAGIC;
  uint32_t version = CURRENT_VERSION;
  char name[64] = {};           // Display name (user-visible)
  char slug[64] = {};           // Filesystem-safe identifier
  uint32_t seed = 0;
  int32_t gameMode = 0;         // 0 = Survival, 1 = Creative
  int64_t lastPlayedTimestamp = 0; // Seconds since epoch

  auto displayName() const -> std::string { return {name, strnlen(name, sizeof(name))}; }
  auto displaySlug() const -> std::string { return {slug, strnlen(slug, sizeof(slug))}; }

  /// Serialize to a binary file at the given path.
  /// Uses write-to-temp + rename for atomicity.
  auto write(const std::filesystem::path& filePath) const -> bool {
    // Write to a temp file first, then rename for crash safety
    auto tmpPath = filePath;
    tmpPath += ".tmp";

    {
      std::ofstream file(tmpPath, std::ios::binary);
      if (!file) return false;
      file.write(reinterpret_cast<const char*>(this), sizeof(*this));
      if (!file.good()) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        return false;
      }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, filePath, ec);
    return !ec;
  }

  /// Deserialize from a binary file.
  static auto read(const std::filesystem::path& filePath) -> std::optional<WorldMetadata> {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return std::nullopt;

    WorldMetadata meta{};
    file.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    if (!file.good()) return std::nullopt;
    if (meta.magic != MAGIC) return std::nullopt;

    return meta;
  }

  /// Create a fresh metadata struct with current timestamp.
  static auto create(const std::string& worldName,
                      const std::string& worldSlug,
                      uint32_t worldSeed,
                      int32_t worldGameMode) -> WorldMetadata {
    WorldMetadata meta{};
    std::strncpy(meta.name, worldName.c_str(), sizeof(meta.name) - 1);
    std::strncpy(meta.slug, worldSlug.c_str(), sizeof(meta.slug) - 1);
    meta.seed = worldSeed;
    meta.gameMode = worldGameMode;
    meta.lastPlayedTimestamp = static_cast<int64_t>(std::time(nullptr));
    return meta;
  }

  /// Touch the timestamp to "now" without changing other fields.
  void touch() { lastPlayedTimestamp = static_cast<int64_t>(std::time(nullptr)); }
};

} // namespace terrain
