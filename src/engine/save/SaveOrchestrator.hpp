#pragma once

#include "WorldMetadata.hpp"
#include "WorldNamingService.hpp"
#include "WorldListService.hpp"
#include "SettingsRepository.hpp"
#include "SaveManager.hpp"
#include "WorldListEntry.hpp"
#include "game/GameMode.hpp"
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <filesystem>

namespace terrain {

/// High-level facade over the save subsystem.
/// Owns naming, listing, settings, and coordinates world create/load operations
/// so that Game and other consumers don't need to wire individual services.
class SaveOrchestrator {
public:
  explicit SaveOrchestrator(std::filesystem::path saveDir);

  // ---- Settings ----

  [[nodiscard]] auto settings() -> SettingsRepository& { return *m_settings; }

  /// Persist runtime settings to the SQLite store.
  void saveSettings(int32_t renderDistance, bool showFps);

  /// Load persisted settings; returns defaults if no saved values exist.
  struct LoadedSettings {
    int32_t renderDistance = 8;
    bool showFps = true;
  };
  [[nodiscard]] auto loadSettings() -> LoadedSettings;

  // ---- World list ----

  [[nodiscard]] auto worldList() const -> const std::vector<WorldMetadata>& {
    return m_worldList->worlds();
  }
  void refreshWorldList() { m_worldList->refresh(); }

  /// Convert internal metadata list to WorldEntry records shared with the UI.
  static auto buildWorldEntries(const std::vector<WorldMetadata>& worlds)
    -> std::vector<WorldEntry>;

  // ---- World operations ----

  /// Result of a prepare-world operation.
  struct PrepareResult {
    std::string slug;
    std::string error; // empty on success
  };

  /// Resolve slug for a new world, checking for name collisions.
  /// Generates a fresh random seed. Returns slug + error string.
  [[nodiscard]] auto prepareNewWorld(const std::string& displayName,
                                      GameMode mode,
                                      uint32_t& outSeed) -> PrepareResult;

  /// Resolve slug for loading an existing world (by display name or slug).
  [[nodiscard]] auto prepareLoadWorld(const std::string& identifier) -> PrepareResult;

  /// Delete a world by its slug, returning true on success.
  auto deleteWorld(const std::string& slug) -> bool;

  /// After WorldController has configured the SaveManager, write/update the
  /// world metadata (name, seed, mode, timestamp).
  void finalizeWorldStart(SaveManager& saveMgr,
                          const std::string& displayName,
                          const std::string& slug,
                          uint32_t seed,
                          GameMode mode);

  /// Called when a world session ends (quit to title, shutdown).
  /// Flushes chunk saves and refreshes the world list.
  void onWorldClosed(SaveManager* saveMgr);

  // ---- Naming ----

  [[nodiscard]] auto naming() -> WorldNamingService& { return *m_naming; }

private:
  std::filesystem::path m_saveDir;
  std::unique_ptr<WorldNamingService> m_naming;
  std::unique_ptr<WorldListService> m_worldList;
  std::unique_ptr<SettingsRepository> m_settings;

  // RNG for world seeds
  std::mt19937 m_seedRng;
};

} // namespace terrain
