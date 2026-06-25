#include "WorldListService.hpp"
#include <algorithm>
#include <chrono>

namespace voxel {

WorldListService::WorldListService(std::filesystem::path saveDir)
  : m_saveDir(std::move(saveDir))
{
  refresh();
}

void WorldListService::refresh() {
  m_worlds.clear();

  if (!std::filesystem::exists(m_saveDir)) {
    return; // No saves directory yet
  }

  for (const auto& entry : std::filesystem::directory_iterator(m_saveDir)) {
    if (!entry.is_directory()) continue;

    auto metaPath = entry.path() / "world.meta";
    if (!std::filesystem::exists(metaPath)) {
      // Legacy world directory without metadata — create on-the-fly metadata
      // so old saves still appear in the list
      auto folderName = entry.path().filename().string();
      auto meta = WorldMetadata::create(folderName, folderName, 0, 0);
      if (meta.write(metaPath)) {
        m_worlds.push_back(std::move(meta));
      }
      continue;
    }

    auto meta = WorldMetadata::read(metaPath);
    if (meta) {
      m_worlds.push_back(std::move(*meta));
    }
  }

  // Sort by last-played timestamp, most recent first
  std::sort(m_worlds.begin(), m_worlds.end(),
    [](const WorldMetadata& a, const WorldMetadata& b) {
      return a.lastPlayedTimestamp > b.lastPlayedTimestamp;
    });
}

auto WorldListService::findBySlug(std::string_view slug) const -> const WorldMetadata* {
  for (const auto& world : m_worlds) {
    if (world.displaySlug() == slug) return &world;
  }
  return nullptr;
}

} // namespace voxel
