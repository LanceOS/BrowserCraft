#include "TerrainEditAPI.hpp"
#include "world/World.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include "world/ChunkCoords.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <ctime>

namespace terrain {

namespace {

/// Check if a sphere intersects a chunk's sample lattice bounding box (which includes the boundary halo).
auto intersectsChunk(int32_t chunkX, int32_t chunkZ, int32_t sizeX, int32_t sizeY, int32_t sizeZ,
                     const glm::vec3& center, float radius) -> bool {
  const float xMin = static_cast<float>(chunkX * sizeX - 1);
  const float xMax = static_cast<float>(chunkX * sizeX + sizeX);
  const float yMin = 0.0f;
  const float yMax = static_cast<float>(sizeY);
  const float zMin = static_cast<float>(chunkZ * sizeZ - 1);
  const float zMax = static_cast<float>(chunkZ * sizeZ + sizeZ);

  const float closestX = std::max(xMin, std::min(center.x, xMax));
  const float closestY = std::max(yMin, std::min(center.y, yMax));
  const float closestZ = std::max(zMin, std::min(center.z, zMax));

  const float dx = center.x - closestX;
  const float dy = center.y - closestY;
  const float dz = center.z - closestZ;
  return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

/// Helper to get density at any integer world coordinates, falling back to procedural generation if needed.
auto getDensityAt(World& world, const WorldGenPipeline& pipeline, int32_t wx, int32_t wy, int32_t wz,
                  int32_t chunkSize, int32_t worldHeight) -> float {
  if (wy < 0 || wy > worldHeight) {
    return wy < 0 ? -1.0f : 1.0f;
  }

  const int32_t cx = floorToChunk(wx, chunkSize);
  const int32_t cz = floorToChunk(wz, chunkSize);
  const Chunk* chunk = world.getChunk(cx, cz);

  if (chunk) {
    ChunkSlot slot = world.getChunkSlot(*chunk);
    if (*slot.densityInitialized == 0) {
      return pipeline.sampleDensity(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz));
    }
    int32_t localX = wx - cx * chunkSize + 1;
    int32_t localZ = wz - cz * chunkSize + 1;
    const int32_t sampleXCount = chunkSize + 2;
    const int32_t sampleZCount = chunkSize + 2;

    localX = std::clamp(localX, 0, sampleXCount - 1);
    localZ = std::clamp(localZ, 0, sampleZCount - 1);

    const size_t idx = (static_cast<size_t>(wy) * sampleZCount + localZ) * sampleXCount + localX;
    return slot.density[idx];
  }

  return pipeline.sampleDensity(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz));
}

/// Helper to populate a chunk slot's density buffer from the procedural generation pipeline.
void initializeDensityBuffer(ChunkSlot& slot, int32_t chunkX, int32_t chunkZ, const WorldGenPipeline& pipeline,
                              int32_t chunkSize, int32_t worldHeight) {
  const int32_t sx = chunkSize;
  const int32_t sy = worldHeight;
  const int32_t sz = chunkSize;
  const int32_t sampleXCount = sx + 2;
  const int32_t sampleYCount = sy + 1;
  const int32_t sampleZCount = sz + 2;
  const int32_t sampleMinX = -1;
  const int32_t sampleMinZ = -1;

  for (int32_t y = 0; y < sampleYCount; ++y) {
    const float worldY = static_cast<float>(y);
    for (int32_t z = 0; z < sampleZCount; ++z) {
      const float worldZ = static_cast<float>(chunkZ * sz + sampleMinZ + z);
      for (int32_t x = 0; x < sampleXCount; ++x) {
        const float worldX = static_cast<float>(chunkX * sx + sampleMinX + x);
        const size_t idx = (static_cast<size_t>(y) * sampleZCount + z) * sampleXCount + x;
        slot.density[idx] = pipeline.sampleDensity(worldX, worldY, worldZ);
      }
    }
  }
  *slot.densityInitialized = 1;
}

} // namespace

void TerrainEditAPI::applyBrush(World& world, const WorldGenPipeline& pipeline, const TerrainBrush& brush) {
  const auto& config = world.config();
  const int32_t sx = config.chunkSize;
  const int32_t sy = config.worldHeight;
  const int32_t sz = config.chunkSize;

  // Narrow down the search space to chunks in the AABB of the brush sphere
  const int32_t minCX = floorToChunk(static_cast<int32_t>(std::floor(brush.center.x - brush.radius - 1.0f)), sx);
  const int32_t maxCX = floorToChunk(static_cast<int32_t>(std::ceil(brush.center.x + brush.radius + 1.0f)), sx);
  const int32_t minCZ = floorToChunk(static_cast<int32_t>(std::floor(brush.center.z - brush.radius - 1.0f)), sz);
  const int32_t maxCZ = floorToChunk(static_cast<int32_t>(std::ceil(brush.center.z + brush.radius + 1.0f)), sz);

  std::vector<Chunk*> affectedChunks;
  for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
    for (int32_t cx = minCX; cx <= maxCX; ++cx) {
      Chunk* chunk = world.getChunkMut(cx, cz);
      if (chunk) {
        if (intersectsChunk(cx, cz, sx, sy, sz, brush.center, brush.radius)) {
          affectedChunks.push_back(chunk);
        }
      }
    }
  }

  if (affectedChunks.empty()) return;

  // Ensure all affected chunks have their density buffers initialized
  for (auto* chunk : affectedChunks) {
    ChunkSlot slot = world.getChunkSlot(*chunk);
    if (*slot.densityInitialized == 0) {
      initializeDensityBuffer(slot, chunk->chunkX, chunk->chunkZ, pipeline, sx, sy);
    }
  }

  // To avoid ordering bias, the Smooth brush requires a 2-pass approach
  struct DensityWrite {
    ChunkSlot slot;
    size_t index;
    float value;
  };
  std::vector<DensityWrite> smoothWrites;

  const int32_t sampleXCount = sx + 2;
  const int32_t sampleZCount = sz + 2;

  for (auto* chunk : affectedChunks) {
    ChunkSlot slot = world.getChunkSlot(*chunk);

    for (int32_t y = 0; y <= sy; ++y) {
      const float worldY = static_cast<float>(y);
      for (int32_t z = -1; z <= sz; ++z) {
        const float worldZ = static_cast<float>(chunk->chunkZ * sz + z);
        for (int32_t x = -1; x <= sx; ++x) {
          const float worldX = static_cast<float>(chunk->chunkX * sx + x);

          const float dx = worldX - brush.center.x;
          const float dy = worldY - brush.center.y;
          const float dz = worldZ - brush.center.z;
          const float distSq = dx * dx + dy * dy + dz * dz;

          if (distSq <= brush.radius * brush.radius) {
            const float dist = std::sqrt(distSq);
            const size_t idx = (static_cast<size_t>(y) * sampleZCount + (z + 1)) * sampleXCount + (x + 1);

            if (brush.type == BrushType::SubtractSphere) {
              const float falloff = 1.0f - (dist / brush.radius);
              const float delta = falloff * brush.strength;
              slot.density[idx] += delta;
              slot.density[idx] = std::clamp(slot.density[idx], -500.0f, 500.0f);
            } else if (brush.type == BrushType::AddSphere) {
              const float falloff = 1.0f - (dist / brush.radius);
              const float delta = falloff * brush.strength;
              slot.density[idx] -= delta;
              slot.density[idx] = std::clamp(slot.density[idx], -500.0f, 500.0f);
            } else if (brush.type == BrushType::Flatten) {
              const float target = glm::dot(glm::vec3(worldX, worldY, worldZ) - brush.center, brush.planeNormal);
              const float falloff = 1.0f - (dist / brush.radius);
              const float blend = std::clamp(falloff * brush.strength, 0.0f, 1.0f);
              slot.density[idx] = (1.0f - blend) * slot.density[idx] + blend * target;
              slot.density[idx] = std::clamp(slot.density[idx], -500.0f, 500.0f);
            } else if (brush.type == BrushType::Smooth) {
              const int32_t wx = static_cast<int32_t>(std::round(worldX));
              const int32_t wy = y;
              const int32_t wz = static_cast<int32_t>(std::round(worldZ));

              const float self = slot.density[idx];
              float sum = 0.0f;
              sum += getDensityAt(world, pipeline, wx - 1, wy, wz, sx, sy);
              sum += getDensityAt(world, pipeline, wx + 1, wy, wz, sx, sy);
              sum += getDensityAt(world, pipeline, wx, wy - 1, wz, sx, sy);
              sum += getDensityAt(world, pipeline, wx, wy + 1, wz, sx, sy);
              sum += getDensityAt(world, pipeline, wx, wy, wz - 1, sx, sy);
              sum += getDensityAt(world, pipeline, wx, wy, wz + 1, sx, sy);
              const float avg = sum / 6.0f;

              const float falloff = 1.0f - (dist / brush.radius);
              const float blend = std::clamp(falloff * brush.strength, 0.0f, 1.0f);
              const float newValue = (1.0f - blend) * self + blend * avg;
              smoothWrites.push_back({slot, idx, std::clamp(newValue, -500.0f, 500.0f)});
            }
          }
        }
      }
    }
  }

  // Apply smoothing edits in second pass to preserve filter symmetry
  if (brush.type == BrushType::Smooth) {
    for (const auto& write : smoothWrites) {
      write.slot.density[write.index] = write.value;
    }
  }

  // Mark all affected chunks dirty and request a remesh
  for (auto* chunk : affectedChunks) {
    world.markChunkDirty(chunk->chunkX, chunk->chunkZ);
    world.requestRemesh(*chunk);
  }

  // Record the edit in the persistence backend, or directly in the history if no persistence is attached.
  if (auto* persistence = world.persistence()) {
    persistence->recordTerrainEdit(brush);
  } else {
    world.editHistory().addEdit(brush, static_cast<uint64_t>(std::time(nullptr)));
  }
}

} // namespace terrain
