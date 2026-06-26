#include "World.hpp"
#include "WorldCoords.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace voxel {

World::World(SharedPool& pool,
             BlockRegistry& blocks,
             const GameConfig& config,
             IChunkWorker& worker,
             IChunkPersistence* persistence)
  : m_pool(pool), m_blocks(blocks), m_config(config),
    m_store(pool, blocks, m_chunks, config),
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
  return center && chunkHasVoxelData(*center);
}

auto World::getChunk(int32_t cx, int32_t cz) const -> const Chunk* {
  return m_chunks.get(cx, cz);
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

auto World::resolveBlock(int32_t worldX, int32_t worldY, int32_t worldZ) -> std::optional<WorldBlockRef> {
  if (worldY < 0 || worldY >= m_config.worldHeight) return std::nullopt;
  int32_t cx = worldToChunk(worldX, m_config.chunkSize);
  int32_t cz = worldToChunk(worldZ, m_config.chunkSize);
  auto* chunk = m_chunks.get(cx, cz);
  if (!chunk) return std::nullopt;
  int32_t localX = mod(worldX, m_config.chunkSize);
  int32_t localZ = mod(worldZ, m_config.chunkSize);
  int32_t idx = (worldY * m_config.chunkSize + localZ) * m_config.chunkSize + localX;
  return WorldBlockRef{chunk, localX, localZ, idx};
}

auto World::getBlockIdAt(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t {
  return m_store.getBlockId(worldX, worldY, worldZ);
}

auto World::getBlockId(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t {
  return m_store.getBlockId(worldX, worldY, worldZ);
}

auto World::setBlockIdAt(int32_t worldX, int32_t worldY, int32_t worldZ, uint8_t blockId) -> bool {
  auto ref = resolveBlock(worldX, worldY, worldZ);
  if (!ref) return false;
  auto slot = m_pool.view(ref->chunk->slotIndex);
  if (slot.voxels[ref->index] == blockId) return false;
  m_store.setBlockId(*ref->chunk, worldY, ref->localX, ref->localZ, blockId);
  if (blockId == 0) slot.redstone[ref->index] = 0;
  markChunkDirty(ref->chunk->chunkX, ref->chunk->chunkZ);
  auto* mutChunk = m_chunks.getMut(ref->chunk->chunkX, ref->chunk->chunkZ);
  if (mutChunk) {
    requestRemesh(*mutChunk);
    // Defer boundary neighbor remeshes — flushed at end of update()
    if (ref->localX == 0 || ref->localX == m_config.chunkSize - 1 ||
        ref->localZ == 0 || ref->localZ == m_config.chunkSize - 1) {
      m_remeshScheduler.noteBoundaryEdit(ref->chunk->chunkX, ref->chunk->chunkZ);
    }
  }
  return true;
}

auto World::getRedstonePackedAt(int32_t worldX, int32_t worldY, int32_t worldZ) const -> uint8_t {
  auto ref = const_cast<World*>(this)->resolveBlock(worldX, worldY, worldZ);
  if (!ref) return 0;
  auto slot = const_cast<World*>(this)->m_pool.view(ref->chunk->slotIndex);
  return slot.redstone[ref->index];
}

auto World::setRedstonePackedAt(int32_t worldX, int32_t worldY, int32_t worldZ, uint8_t packed) -> bool {
  auto ref = resolveBlock(worldX, worldY, worldZ);
  if (!ref) return false;
  auto slot = m_pool.view(ref->chunk->slotIndex);
  if (slot.redstone[ref->index] == packed) return false;
  slot.redstone[ref->index] = packed;
  markChunkDirty(ref->chunk->chunkX, ref->chunk->chunkZ);
  auto* mutChunk = m_chunks.getMut(ref->chunk->chunkX, ref->chunk->chunkZ);
  if (mutChunk) {
    requestRemesh(*mutChunk);
    // Defer boundary neighbor remeshes
    if (ref->localX == 0 || ref->localX == m_config.chunkSize - 1 ||
        ref->localZ == 0 || ref->localZ == m_config.chunkSize - 1) {
      m_remeshScheduler.noteBoundaryEdit(ref->chunk->chunkX, ref->chunk->chunkZ);
    }
  }
  return true;
}

auto World::isSolidInChunk(int32_t worldX, int32_t worldY, int32_t worldZ, const Chunk& chunk) const -> bool {
  return m_store.isSolidInChunk(worldX, worldY, worldZ, chunk);
}

auto World::isSolid(int32_t worldX, int32_t worldY, int32_t worldZ) const -> bool {
  return m_store.isSolid(worldX, worldY, worldZ);
}

auto World::isFluid(int32_t worldX, int32_t worldY, int32_t worldZ) const -> bool {
  return m_store.isFluid(worldX, worldY, worldZ);
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
  // @see notes/world-switch-stale-callbacks.md
  auto* chunk = getChunkBySlotIndex(slotIndex);
  if (!chunk) return;
  if (chunk->state != ChunkState::Generating) return;

  // ---- Validate generated chunk data ----
  // Check that bedrock was placed at y=0. If generation ran correctly
  // bedrock will always be present; its absence indicates a silent failure.
  {
    auto slot = m_pool.view(chunk->slotIndex);
    const int32_t sx = m_config.chunkSize;
    const int32_t sz = m_config.chunkSize;
    // Sample a few columns at the chunk corners.
    bool hasBedrock = false;
    const int32_t checkIndices[] = {0, sz - 1, (sz - 1) * sx, sz * sx - 1};
    for (int32_t ci : checkIndices) {
      if (slot.voxels[ci] != 0) { hasBedrock = true; break; }
    }
    if (!hasBedrock && chunk->genRetries < MAX_CHUNK_GEN_RETRIES) {
      ++chunk->genRetries;
      chunk->state = ChunkState::QueuedGen;
      m_jobQueue.pushGen(chunk->slotIndex, chunk->chunkX, chunk->chunkZ);
      return;
    }
  }

  chunk->state = ChunkState::VoxelsReady;
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

void World::onSaveLoadSuccess(int32_t chunkX, int32_t chunkZ,
                              const uint8_t* voxels, const uint8_t* light, const uint8_t* redstone,
                              size_t dataSize) {
  auto* chunk = m_chunks.getMut(chunkX, chunkZ);
  if (!chunk) return;
  if (chunk->state != ChunkState::LoadingFromDisk) return;
  auto slot = m_pool.view(chunk->slotIndex);
  *slot.chunkX = chunkX;
  *slot.chunkZ = chunkZ;
  std::memcpy(slot.voxels, voxels, dataSize);
  std::memcpy(slot.light, light, dataSize);
  std::memcpy(slot.redstone, redstone, dataSize);
  *slot.status = static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
  chunk->state = ChunkState::VoxelsReady;
  chunk->needsRemesh = false;
  m_jobQueue.pushMesh(chunk->slotIndex, chunk->chunkX, chunk->chunkZ);
  requestNeighborRemeshes(*chunk);
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
  // Iterate square bounding box, skip chunks outside the Euclidean circle.
  // This reduces loaded chunks by ~21.5% vs a full square (cylindrical loading).
  for (int32_t dz = -r; dz <= r; ++dz) {
    for (int32_t dx = -r; dx <= r; ++dx) {
      if (dx * dx + dz * dz > r2) continue;

      int32_t cx = centerCX + dx;
      int32_t cz = centerCZ + dz;
      if (m_chunks.has(cx, cz)) continue;

      auto slot = m_pool.acquire();
      if (!slot) return;

      Chunk chunk{cx, cz, slot->slotIndex};
      *slot->chunkX = cx;
      *slot->chunkZ = cz;
      m_chunks.set(chunk);

      // Store slot coordinates — map-backed chunks can rehash, so pointers are not stable.
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
      chunk.state == ChunkState::VoxelsReady) {
    return;
  }
  chunk.state = ChunkState::QueuedMesh;
  m_jobQueue.pushMesh(chunk.slotIndex, chunk.chunkX, chunk.chunkZ);
}

void World::markChunkDirty(int32_t cx, int32_t cz) {
  if (m_persistence) m_persistence->markDirty(cx, cz);
}

auto World::chunkHasVoxelData(const Chunk& chunk) const -> bool {
  return chunk.state == ChunkState::VoxelsReady ||
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
  if (!neighbor || !chunkHasVoxelData(*neighbor)) return;
  requestRemesh(*neighbor);
}

} // namespace voxel
