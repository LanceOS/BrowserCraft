#include "SaveManager.hpp"
#include "../alloc/SharedPool.hpp"
#include "../../world/Chunk.hpp"
#include "../../world/World.hpp"
#include "../threading/WorkerThreadPool.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>

namespace voxel {

SaveManager::SaveManager(const std::string& saveDir, const std::string& slotId,
                         SharedPool& pool, World& world, WorkerThreadPool* ioPool)
  : m_saveDir(saveDir), m_slotId(slotId), m_pool(pool), m_world(world), m_ioPool(ioPool) {
  std::filesystem::create_directories(m_saveDir + "/" + m_slotId);
  auto dims = pool.dimensions();
  m_dataSize = static_cast<size_t>(dims.sizeX) * dims.sizeY * dims.sizeZ;

  // Load existing metadata, or create default metadata for legacy worlds
  auto metaPath = metadataFilePath();
  auto existing = WorldMetadata::read(metaPath);
  if (existing) {
    m_metadata = std::move(*existing);
  } else {
    // Legacy world or first save — create default metadata from the slot ID
    m_metadata = WorldMetadata::create(m_slotId, m_slotId, 0, 0);
    m_metadata.write(metaPath);
  }
}

SaveManager::~SaveManager() { flushPending(); }

auto SaveManager::chunkFilePath(int32_t cx, int32_t cz) const -> std::string {
  return m_saveDir + "/" + m_slotId + "/" + std::to_string(cx) + "_" + std::to_string(cz) + ".chunk";
}

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
  // Fast path: if the chunk file doesn't exist on disk, fail immediately
  // instead of submitting an async I/O task that would just return false.
  // This avoids a multi-frame delay for every chunk on a fresh world.
  if (!std::filesystem::exists(chunkFilePath(chunkX, chunkZ))) {
    m_world.onSaveLoadFailed(chunkX, chunkZ);
    return;
  }

  // File exists — submit async I/O to the thread pool; the main thread
  // processes the result via processPending().
  size_t ds = m_dataSize;
  m_ioPool->submitAndForget([this, chunkX, chunkZ, ds]() {
    PendingChunkLoad result{chunkX, chunkZ, {}, {}, {}, false};
    result.voxels.resize(ds);
    result.light.resize(ds);
    result.redstone.resize(ds);

    result.success = loadChunk(chunkX, chunkZ,
                                result.voxels.data(),
                                result.light.data(),
                                result.redstone.data(), ds);

    std::lock_guard lock(m_loadMutex);
    m_pendingLoads.push(std::move(result));
  });
}

void SaveManager::markDirty(int32_t chunkX, int32_t chunkZ) {
  {
    std::lock_guard lock(m_mutex);
    // If already dirty, skip duplicate submission
    if (!m_dirtyChunks.insert(chunkKey(chunkX, chunkZ)).second) return;
  }

  // Submit async save to I/O thread pool — the chunk data is copied
  // on the worker thread so the main thread is not blocked.
  size_t ds = m_dataSize;
  m_ioPool->submitAndForget([this, chunkX, chunkZ, ds]() {
    auto* chunk = m_world.getChunk(chunkX, chunkZ);
    if (!chunk) return;
    auto slot = m_pool.view(chunk->slotIndex);

    std::ofstream file(chunkFilePath(chunkX, chunkZ), std::ios::binary);
    if (!file) return;

    int32_t header[3] = {chunkX, chunkZ, static_cast<int32_t>(ds)};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(reinterpret_cast<const char*>(slot.voxels), ds);
    file.write(reinterpret_cast<const char*>(slot.light), ds);
    file.write(reinterpret_cast<const char*>(slot.redstone), ds);

    // Clear dirty flag on success
    if (file.good()) {
      std::lock_guard lock(m_mutex);
      m_dirtyChunks.erase(chunkKey(chunkX, chunkZ));
    }
  });
}

void SaveManager::flushPending() {
  // Drain any remaining dirty chunks that weren't picked up by async saves.
  // This runs synchronously on the main thread (e.g. on quit).
  std::unordered_set<int64_t> remaining;
  {
    std::lock_guard lock(m_mutex);
    remaining.swap(m_dirtyChunks);
  }
  for (int64_t key : remaining) {
    int32_t cx = static_cast<int32_t>(key >> 32);
    int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFF);
    saveChunk(cx, cz);
  }
}

auto SaveManager::saveChunk(int32_t chunkX, int32_t chunkZ) -> bool {
  auto* chunk = m_world.getChunk(chunkX, chunkZ);
  if (!chunk) return false;
  auto slot = m_pool.view(chunk->slotIndex);

  std::ofstream file(chunkFilePath(chunkX, chunkZ), std::ios::binary);
  if (!file) return false;

  // Simple binary format: chunkX(4), chunkZ(4), dataSize(4), voxels, light, redstone
  int32_t header[3] = {chunkX, chunkZ, static_cast<int32_t>(m_dataSize)};
  file.write(reinterpret_cast<const char*>(header), sizeof(header));
  file.write(reinterpret_cast<const char*>(slot.voxels), m_dataSize);
  file.write(reinterpret_cast<const char*>(slot.light), m_dataSize);
  file.write(reinterpret_cast<const char*>(slot.redstone), m_dataSize);
  return file.good();
}

auto SaveManager::loadChunk(int32_t chunkX, int32_t chunkZ,
                             uint8_t* outVoxels, uint8_t* outLight, uint8_t* outRedstone,
                             size_t dataSize) -> bool {
  std::ifstream file(chunkFilePath(chunkX, chunkZ), std::ios::binary);
  if (!file) return false;

  int32_t header[3];
  file.read(reinterpret_cast<char*>(header), sizeof(header));
  if (!file || header[0] != chunkX || header[1] != chunkZ) return false;

  size_t storedSize = static_cast<size_t>(header[2]);
  size_t readSize = std::min(dataSize, storedSize);

  file.read(reinterpret_cast<char*>(outVoxels), readSize);
  file.read(reinterpret_cast<char*>(outLight), readSize);
  file.read(reinterpret_cast<char*>(outRedstone), readSize);
  return file.good();
}

void SaveManager::processPending() {
  std::queue<PendingChunkLoad> ready;
  {
    std::lock_guard lock(m_loadMutex);
    ready.swap(m_pendingLoads);
  }

  while (!ready.empty()) {
    auto& load = ready.front();
    if (load.success) {
      m_world.onSaveLoadSuccess(load.chunkX, load.chunkZ,
                                 load.voxels.data(),
                                 load.light.data(),
                                 load.redstone.data(),
                                 load.voxels.size());
    } else {
      m_world.onSaveLoadFailed(load.chunkX, load.chunkZ);
    }
    ready.pop();
  }
}

} // namespace voxel
