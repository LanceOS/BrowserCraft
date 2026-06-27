#include "SaveManager.hpp"
#include "TerrainSaveData.hpp"
#include "../alloc/SharedPool.hpp"
#include "../../world/Chunk.hpp"
#include "../../world/World.hpp"
#include "../threading/WorkerThreadPool.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <ctime>

namespace terrain {

SaveManager::SaveManager(const std::string& saveDir, const std::string& slotId,
                         SharedPool& pool, World& world, WorkerThreadPool* ioPool)
  : m_saveDir(saveDir), m_slotId(slotId), m_pool(pool), m_world(world), m_ioPool(ioPool) {
  std::filesystem::create_directories(m_saveDir + "/" + m_slotId);

  // Load existing metadata, or create default metadata for legacy worlds
  auto metaPath = metadataFilePath();
  auto existing = WorldMetadata::read(metaPath);
  if (existing) {
    m_metadata = std::move(*existing);
  } else {
    m_metadata = WorldMetadata::create(m_slotId, m_slotId, 0, 0);
    m_metadata.write(metaPath);
  }

  // Load terrain edits history
  std::string editsPath = m_saveDir + "/" + m_slotId + "/terrain.edits";
  TerrainSaveData::load(editsPath, m_world.editHistory());
}

SaveManager::~SaveManager() {}

auto SaveManager::metadataFilePath() const -> std::string {
  return m_saveDir + "/" + m_slotId + "/world.meta";
}

void SaveManager::writeMetadata(const WorldMetadata& meta) {
  m_metadata = meta;
  m_metadata.write(metadataFilePath());
}

void SaveManager::touchMetadata() {
  m_metadata.touch();
  m_metadata.write(metadataFilePath());
}

void SaveManager::requestLoad(int32_t chunkX, int32_t chunkZ) {
  // Since individual chunk files are removed, report load failure immediately.
  // This causes the World to fall back to generating the chunk from the seed
  // and replaying all manual terrain edits.
  m_world.onSaveLoadFailed(chunkX, chunkZ);
}

void SaveManager::markDirty(int32_t /*chunkX*/, int32_t /*chunkZ*/) {
  // No-op: individual chunk files are no longer saved as the world state is
  // fully represented by the seed and the terrain edit log.
}

void SaveManager::recordTerrainEdit(const TerrainBrush& brush) {
  uint64_t timestamp = static_cast<uint64_t>(std::time(nullptr));
  TerrainEdit edit{brush, timestamp};

  // 1. Add to the world's edit history so any future chunk generations see it immediately
  m_world.editHistory().addEdit(edit);

  // 2. Append to the save file
  std::string filePath = m_saveDir + "/" + m_slotId + "/terrain.edits";
  if (m_ioPool) {
    m_ioPool->submitAndForget([filePath, edit]() {
      TerrainSaveData::append(filePath, edit);
    });
  } else {
    TerrainSaveData::append(filePath, edit);
  }
}

} // namespace terrain
