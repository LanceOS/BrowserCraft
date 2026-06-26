#include "World.hpp"
#include "ChunkCoords.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace terrain {
namespace {

inline auto redstoneIndex(int32_t worldY, int32_t localX, int32_t localZ, int32_t chunkSize) -> int32_t {
  return (worldY * chunkSize + localZ) * chunkSize + localX;
}

} // namespace

World::World(SharedPool& pool,
             const GameConfig& config,
             IChunkWorker& worker,
             IChunkPersistence* persistence)
  : m_pool(pool), m_config(config),
    m_jobQueue(worker),
    m_persistence(persistence)
{}

void World::update(glm::vec3 cameraPos) {
  int32_t cx = worldToChunk(cameraPos.x, m_config.chunkSize);
  int32_t cz = worldToChunk(cameraPos.z, m_config.chunkSize);

  m_centerChunkX = cx;
  m_centerChunkZ = cz;
  m_hasCenter = true;

  ensureVisibleRadius(cx, cz);
  unloadFarChunks(cx, cz);
  m_jobQueue.pump(m_chunks, m_pool, m_slotToChunk, m_config.worldSeed);
  m_remeshScheduler.flush([this](int32_t chunkX, int32_t chunkZ) {
    auto* chunk = m_chunks.getMut(chunkX, chunkZ);
    if (!chunk) return;
    requestBoundaryNeighborRemeshes(*chunk, 0, 0);
    requestBoundaryNeighborRemeshes(*chunk, m_config.chunkSize - 1, m_config.chunkSize - 1);
  });
}

auto World::isReady() const -> bool {
  if (!m_hasCenter) return false;
  auto* center = m_chunks.get(m_centerChunkX, m_centerChunkZ);
  return center && chunkHasMeshes(*center);
}

auto World::hasTerrain() const -> bool {
  if (!m_hasCenter) return false;
  auto* center = m_chunks.get(m_centerChunkX, m_centerChunkZ);
  return center && chunkHasTerrainData(*center);
}

auto World::getChunk(int32_t cx, int32_t cz) const -> const Chunk* {
  return m_chunks.get(cx, cz);
}

auto World::getChunkMut(int32_t cx, int32_t cz) -> Chunk* {
  return m_chunks.getMut(cx, cz);
}

auto World::getChunkBySlotIndex(int32_t slotIndex) const -> const Chunk* {
  auto it = m_slotToChunk.find(slotIndex);
  if (it == m_slotToChunk.end()) return nullptr;
  return m_chunks.get(it->second.cx, it->second.cz);
}

auto World::getChunkBySlotIndex(int32_t slotIndex) -> Chunk* {
  auto it = m_slotToChunk.find(slotIndex);
  if (it == m_slotToChunk.end()) return nullptr;
  return m_chunks.getMut(it->second.cx, it->second.cz);
}

auto World::getChunkSlot(const Chunk& chunk) -> ChunkSlot {
  return m_pool.view(chunk.slotIndex);
}

auto World::getRedstonePackedAt(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t {
  if (worldY < 0 || worldY >= m_config.worldHeight) return 0;
  int32_t cx = floorToChunk(worldX, m_config.chunkSize);
  int32_t cz = floorToChunk(worldZ, m_config.chunkSize);
  const auto* chunk = m_chunks.get(cx, cz);
  if (!chunk) return 0;
  int32_t localX = mod(worldX, m_config.chunkSize);
  int32_t localZ = mod(worldZ, m_config.chunkSize);
  auto slot = m_pool.view(chunk->slotIndex);
  return slot.redstone[redstoneIndex(worldY, localX, localZ, m_config.chunkSize)];
}

auto World::setRedstonePackedAt(int32_t worldX, int32_t worldY, int32_t worldZ, uint8_t packed) -> bool {
  if (worldY < 0 || worldY >= m_config.worldHeight) return false;
  int32_t cx = floorToChunk(worldX, m_config.chunkSize);
  int32_t cz = floorToChunk(worldZ, m_config.chunkSize);
  auto* chunk = m_chunks.getMut(cx, cz);
  if (!chunk) return false;

  int32_t localX = mod(worldX, m_config.chunkSize);
  int32_t localZ = mod(worldZ, m_config.chunkSize);
  auto slot = m_pool.view(chunk->slotIndex);
  int32_t idx = redstoneIndex(worldY, localX, localZ, m_config.chunkSize);

  if (slot.redstone[idx] == packed) return false;
  slot.redstone[idx] = packed;
  markChunkDirty(chunk->chunkX, chunk->chunkZ);
  requestRemesh(*chunk);

  // Defer boundary neighbor remeshes
  if (localX == 0 || localX == m_config.chunkSize - 1 ||
      localZ == 0 || localZ == m_config.chunkSize - 1) {
    m_remeshScheduler.noteBoundaryEdit(chunk->chunkX, chunk->chunkZ);
  }
  return true;
}

void World::markUploaded(const Chunk& chunk) {
  auto* mutChunk = m_chunks.getMut(chunk.chunkX, chunk.chunkZ);
  if (mutChunk) mutChunk->state = ChunkState::Uploaded;
  auto slot = m_pool.view(chunk.slotIndex);
  *slot.status = static_cast<int32_t>(ChunkSlotStatus::GPU_UPLOADED);
}

void World::clear() {
  std::vector<int32_t> usedSlots;
  m_chunks.forEach([&](const Chunk& chunk) { usedSlots.push_back(chunk.slotIndex); });

  for (const int32_t slotIndex : usedSlots) {
    m_slotToChunk.erase(slotIndex);
    m_pool.release(m_pool.view(slotIndex));
  }

  m_chunks = ChunkManager{};
  m_slotToChunk.clear();
  m_jobQueue.clear();
  m_pendingUploadSlots.clear();
  m_remeshScheduler.clear();
  m_hasCenter = false;
}

void World::onWorldGenDone(int32_t slotIndex) {
  auto* chunk = getChunkBySlotIndex(slotIndex);
  if (!chunk) return;
  if (chunk->state != ChunkState::Generating) return;

  chunk->state = ChunkState::DensityReady;
  chunk->needsRemesh = false;
  markChunkDirty(chunk->chunkX, chunk->chunkZ);
  m_jobQueue.pushMesh(chunk->slotIndex, chunk->chunkX, chunk->chunkZ);
  requestNeighborRemeshes(*chunk);
}

void World::onMeshDone(int32_t slotIndex, uint32_t vertexCount, uint32_t indexCount, bool success) {
  auto* chunk = getChunkBySlotIndex(slotIndex);
  if (!chunk) return;
  if (chunk->state != ChunkState::Meshing) return;
  chunk->vertexCount = vertexCount;
  chunk->indexCount = indexCount;
  if (success) {
    chunk->meshRestartRetries = 0;
    chunk->state = ChunkState::MeshReady;
    m_pendingUploadSlots.push_back(slotIndex);
  } else if (chunk->meshRestartRetries < MAX_CHUNK_MESH_RESTART_RETRIES) {
    ++chunk->meshRestartRetries;
    restartChunkFromScratch(*chunk);
    return;
  } else {
    chunk->state = ChunkState::MeshFailed;
  }
  if (chunk->needsRemesh) {
    chunk->needsRemesh = false;
    requestRemesh(*chunk);
  }
}

auto World::drainPendingUploadSlots() -> std::vector<int32_t> {
  return std::move(m_pendingUploadSlots);
}

void World::onSaveLoadFailed(int32_t chunkX, int32_t chunkZ) {
  auto* chunk = m_chunks.getMut(chunkX, chunkZ);
  if (!chunk) return;
  if (chunk->state != ChunkState::LoadingFromDisk) return;
  chunk->state = ChunkState::QueuedGen;
  m_jobQueue.pushGen(chunk->slotIndex, chunk->chunkX, chunk->chunkZ);
}

// ---- Private ----

void World::ensureVisibleRadius(int32_t centerCX, int32_t centerCZ) {
  int32_t r = m_config.renderDistance;
  int32_t r2 = r * r;
  struct ChunkOffset {
    int32_t dx;
    int32_t dz;
  };

  std::vector<ChunkOffset> visibleOffsets;
  visibleOffsets.reserve(static_cast<size_t>((r * 2 + 1) * (r * 2 + 1)));

  for (int32_t dz = -r; dz <= r; ++dz) {
    for (int32_t dx = -r; dx <= r; ++dx) {
      if (dx * dx + dz * dz > r2) continue;
      visibleOffsets.push_back({dx, dz});
    }
  }

  std::sort(visibleOffsets.begin(), visibleOffsets.end(),
            [](const ChunkOffset& a, const ChunkOffset& b) {
              const int32_t distA = a.dx * a.dx + a.dz * a.dz;
              const int32_t distB = b.dx * b.dx + b.dz * b.dz;
              if (distA != distB) return distA < distB;

              const int32_t manhattanA = std::abs(a.dx) + std::abs(a.dz);
              const int32_t manhattanB = std::abs(b.dx) + std::abs(b.dz);
              if (manhattanA != manhattanB) return manhattanA < manhattanB;

              if (a.dz != b.dz) return a.dz < b.dz;
              return a.dx < b.dx;
            });

  for (const ChunkOffset& offset : visibleOffsets) {
    int32_t cx = centerCX + offset.dx;
    int32_t cz = centerCZ + offset.dz;
    if (m_chunks.has(cx, cz)) continue;

    auto slot = m_pool.acquire();
    if (!slot) return;

    Chunk chunk{cx, cz, slot->slotIndex};
    *slot->chunkX = cx;
    *slot->chunkZ = cz;
    m_chunks.set(chunk);

    auto* inserted = m_chunks.getMut(cx, cz);
    if (inserted) {
      m_slotToChunk[chunk.slotIndex] = {cx, cz};
    }

    if (m_persistence) {
      inserted->state = ChunkState::LoadingFromDisk;
      m_persistence->requestLoad(cx, cz);
    } else {
      inserted->state = ChunkState::QueuedGen;
      m_jobQueue.pushGen(inserted->slotIndex, inserted->chunkX, inserted->chunkZ);
    }
  }
}

void World::unloadFarChunks(int32_t centerCX, int32_t centerCZ) {
  int32_t maxDist = m_config.renderDistance + 1;
  int32_t maxDist2 = maxDist * maxDist;
  std::vector<Chunk*> toUnload;

  m_chunks.forEach([&](const Chunk& chunk) {
    int32_t dx = std::abs(chunk.chunkX - centerCX);
    int32_t dz = std::abs(chunk.chunkZ - centerCZ);
    bool canRelease =
      chunk.state == ChunkState::MeshReady ||
      chunk.state == ChunkState::Uploaded ||
      chunk.state == ChunkState::MeshFailed;
    if (dx * dx + dz * dz > maxDist2 && canRelease) {
      toUnload.push_back(const_cast<Chunk*>(&chunk));
    }
  });

  for (auto* chunk : toUnload) {
    m_slotToChunk.erase(chunk->slotIndex);
    m_chunks.remove(chunk->chunkX, chunk->chunkZ);
    m_pool.release(m_pool.view(chunk->slotIndex));
  }
}

void World::restartChunkFromScratch(Chunk& chunk) {
  auto slot = m_pool.view(chunk.slotIndex);
  *slot.vertexCount = 0u;
  *slot.indexCount = 0u;
  *slot.renderFlags = 0u;
  *slot.opaqueIndexCount = 0u;
  *slot.transparentIndexCount = 0u;
  chunk.vertexCount = 0u;
  chunk.indexCount = 0u;
  chunk.opaqueIndexCount = 0u;
  chunk.transparentIndexCount = 0u;
  chunk.hasOpaque = false;
  chunk.hasTransparent = false;
  chunk.needsRemesh = false;

  if (m_persistence) {
    chunk.state = ChunkState::LoadingFromDisk;
    m_persistence->requestLoad(chunk.chunkX, chunk.chunkZ);
  } else {
    chunk.state = ChunkState::QueuedGen;
    m_jobQueue.pushGen(chunk.slotIndex, chunk.chunkX, chunk.chunkZ);
  }
}

void World::requestRemesh(Chunk& chunk) {
  if (chunk.state == ChunkState::LoadingFromDisk) return;
  if (chunk.state == ChunkState::QueuedMesh) return;
  if (chunk.state == ChunkState::Meshing) {
    chunk.needsRemesh = true;
    return;
  }
  if (chunk.state == ChunkState::Generating ||
      chunk.state == ChunkState::QueuedGen ||
      chunk.state == ChunkState::DensityReady) {
    return;
  }
  chunk.state = ChunkState::QueuedMesh;
  m_jobQueue.pushMesh(chunk.slotIndex, chunk.chunkX, chunk.chunkZ);
}

void World::markChunkDirty(int32_t cx, int32_t cz) {
  if (m_persistence) m_persistence->markDirty(cx, cz);
}

auto World::chunkHasTerrainData(const Chunk& chunk) const -> bool {
  return chunk.state == ChunkState::DensityReady ||
         chunk.state == ChunkState::QueuedMesh ||
         chunk.state == ChunkState::Meshing ||
         chunk.state == ChunkState::MeshReady ||
         chunk.state == ChunkState::Uploaded ||
         chunk.state == ChunkState::MeshFailed;
}

auto World::chunkHasMeshes(const Chunk& chunk) const -> bool {
  return chunk.state == ChunkState::MeshReady ||
         chunk.state == ChunkState::Uploaded;
}

void World::requestNeighborRemeshes(const Chunk& chunk) {
  requestNeighborRemesh(chunk, -1, 0);
  requestNeighborRemesh(chunk, 1, 0);
  requestNeighborRemesh(chunk, 0, -1);
  requestNeighborRemesh(chunk, 0, 1);
}

void World::requestBoundaryNeighborRemeshes(const Chunk& chunk, int32_t localX, int32_t localZ) {
  if (localX == 0) requestNeighborRemesh(chunk, -1, 0);
  if (localX == m_config.chunkSize - 1) requestNeighborRemesh(chunk, 1, 0);
  if (localZ == 0) requestNeighborRemesh(chunk, 0, -1);
  if (localZ == m_config.chunkSize - 1) requestNeighborRemesh(chunk, 0, 1);
}

void World::requestNeighborRemesh(const Chunk& chunk, int32_t dx, int32_t dz) {
  auto* neighbor = m_chunks.getMut(chunk.chunkX + dx, chunk.chunkZ + dz);
  if (!neighbor || !chunkHasTerrainData(*neighbor)) return;
  requestRemesh(*neighbor);
}

} // namespace terrain
