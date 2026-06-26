#include "SaveOrchestrator.hpp"
#include <algorithm>

namespace voxel {

SaveOrchestrator::SaveOrchestrator(std::filesystem::path saveDir)
  : m_saveDir(std::move(saveDir)),
    m_naming(std::make_unique<WorldNamingService>(m_saveDir)),
    m_worldList(std::make_unique<WorldListService>(m_saveDir)),
    m_settings(std::make_unique<SettingsRepository>(m_saveDir / "settings.db")),
    m_seedRng(std::random_device{}())
{}

// ---- Settings ----

void SaveOrchestrator::saveSettings(int32_t renderDistance, bool showFps) {
  m_settings->setInt("renderDistance", renderDistance);
  m_settings->setBool("showFps", showFps);
  m_settings->flush();
}

auto SaveOrchestrator::loadSettings() -> LoadedSettings {
  LoadedSettings s;
  s.renderDistance = std::clamp(
      m_settings->getInt("renderDistance", s.renderDistance),
      MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
  s.showFps = m_settings->getBool("showFps", s.showFps);
  return s;
}

// ---- World entries ----

auto SaveOrchestrator::buildWorldEntries(const std::vector<WorldMetadata>& worlds)
  -> std::vector<WorldEntry>
{
  std::vector<WorldEntry> entries;
  entries.reserve(worlds.size());
  for (const auto& meta : worlds) {
    entries.push_back({
      .name = meta.displayName(),
      .slug = meta.displaySlug(),
      .seed = meta.seed,
      .gameMode = meta.gameMode,
      .lastPlayedTimestamp = meta.lastPlayedTimestamp
    });
  }
  return entries;
}

// ---- World operations ----

auto SaveOrchestrator::prepareNewWorld(const std::string& displayName,
                                        GameMode mode,
                                        uint32_t& outSeed) -> PrepareResult
{
  // Validate name
  auto validationError = WorldNamingService::validateName(displayName);
  if (!validationError.empty()) {
    return {{}, std::move(validationError)};
  }

  // Check collision
  if (m_naming->isNameTaken(displayName)) {
    return {{}, "A world with this name already exists. Choose a different name."};
  }

  // Find available slug
  auto slug = m_naming->nextAvailableSlug(displayName);

  // Generate seed
  outSeed = static_cast<uint32_t>(m_seedRng());

  return {std::move(slug), {}};
}

auto SaveOrchestrator::prepareLoadWorld(const std::string& identifier) -> PrepareResult {
  if (identifier.empty()) {
    return {{}, "World name cannot be empty."};
  }

  // Try as slug first
  if (m_naming->isSlugTaken(identifier)) {
    return {WorldNamingService::generateSlug(identifier), {}};
  }

  // Try resolving through the world list by display name
  auto slug = WorldNamingService::generateSlug(identifier);
  if (m_naming->isSlugTaken(slug)) {
    return {std::move(slug), {}};
  }

  return {{}, "No world found with that name."};
}

void SaveOrchestrator::finalizeWorldStart(SaveManager& saveMgr,
                                           const std::string& displayName,
                                           const std::string& slug,
                                           uint32_t seed,
                                           GameMode mode)
{
  auto meta = WorldMetadata::create(
    displayName, slug, seed, static_cast<int32_t>(mode));

  // Check if this is a fresh world (no existing metadata with correct data)
  auto existing = WorldMetadata::read(
    saveMgr.metadataFilePath());
  if (!existing || existing->seed != seed) {
    // New world or different world — write fresh metadata
    saveMgr.writeMetadata(meta);
  } else {
    // Loading existing — just touch the timestamp
    saveMgr.touchMetadata();
  }
}

void SaveOrchestrator::onWorldClosed(SaveManager* saveMgr) {
  if (saveMgr) {
    saveMgr->flushPending();
  }
  m_worldList->refresh();
}

} // namespace voxel
