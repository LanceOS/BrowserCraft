#include "game/ChunkWorkerImpl.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/render/ChunkMeshAllocator.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "game/WorldController.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include "world/mesh/SurfaceNetsMesher.hpp"
#include "world/terrain/TerrainCollision.hpp"
#include "world/ChunkCoords.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include <glm/glm.hpp>

namespace terrain {

namespace {

struct MeshScratchBuffers {
  std::vector<float> vertices;
  std::vector<uint32_t> indices;
};

thread_local MeshScratchBuffers g_meshScratch;

void annotateSurfaceNetsVertices(const WorldGenPipeline& pipeline,
                                 const mesh::SurfaceNetsConfig& cfg,
                                 float* vertexOut,
                                 uint32_t vertexCount) {
  if (!vertexOut) return;

  for (uint32_t i = 0; i < vertexCount; ++i) {
    float* v = vertexOut + static_cast<size_t>(i) * static_cast<size_t>(cfg.strideFloats);
    const float worldX = cfg.originX + v[0];
    const float worldY = cfg.originY + v[1];
    const float worldZ = cfg.originZ + v[2];
    const glm::vec3 normal(v[3], v[4], v[5]);
    const TerrainMaterial material = pipeline.sampleMaterial(worldX, worldY, worldZ, normal);
    v[6] = static_cast<float>(material.primary);
    v[7] = static_cast<float>(material.secondary);
    v[8] = material.blend;
    v[9] = material.tint;
  }
}

struct DensitySlotContext {
  const float* densityBuffer;
  int32_t chunkX;
  int32_t chunkZ;
  int32_t sizeX;
  int32_t sizeY;
  int32_t sizeZ;
  float gridSpacing;
};

auto slotDensitySample(void* userData, float worldX, float worldY, float worldZ) -> float {
  auto* ctx = static_cast<DensitySlotContext*>(userData);
  const int32_t sx = ctx->sizeX;
  const int32_t sy = ctx->sizeY;
  const int32_t sz = ctx->sizeZ;
  const int32_t sampleXCount = sx + 2;
  const int32_t sampleZCount = sz + 2;

  float localXVal = (worldX - static_cast<float>(ctx->chunkX * sx) * ctx->gridSpacing) / ctx->gridSpacing + 1.0f;
  float localYVal = worldY / ctx->gridSpacing;
  float localZVal = (worldZ - static_cast<float>(ctx->chunkZ * sz) * ctx->gridSpacing) / ctx->gridSpacing + 1.0f;

  const int32_t localX = static_cast<int32_t>(std::round(localXVal));
  const int32_t localY = static_cast<int32_t>(std::round(localYVal));
  const int32_t localZ = static_cast<int32_t>(std::round(localZVal));

  const int32_t clampedX = std::clamp(localX, 0, sampleXCount - 1);
  const int32_t clampedY = std::clamp(localY, 0, sy);
  const int32_t clampedZ = std::clamp(localZ, 0, sampleZCount - 1);

  const size_t idx = (static_cast<size_t>(clampedY) * sampleZCount + clampedZ) * sampleXCount + clampedX;
  return ctx->densityBuffer[idx];
}

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

/// Helper to get density at any integer world coordinates, falling back to pipeline if outside the local chunk.
auto getDensityAtReplay(int32_t gx, int32_t gy, int32_t gz,
                        int32_t chunkX, int32_t chunkZ,
                        const float* densityBuffer,
                        const WorldGenPipeline& pipeline,
                        int32_t chunkSize, int32_t worldHeight,
                        float gridSpacing) -> float {
  int32_t scale = static_cast<int32_t>(1.0f / gridSpacing);
  const int32_t sx = chunkSize * scale;
  const int32_t sy = worldHeight * scale;
  const int32_t sz = chunkSize * scale;

  if (gy < 0 || gy > sy) {
    return gy < 0 ? -1.0f : 1.0f;
  }

  const int32_t chunkGridSize = chunkSize * scale;
  const int32_t gcx = gx >= 0 ? gx / chunkGridSize : (gx - chunkGridSize + 1) / chunkGridSize;
  const int32_t gcz = gz >= 0 ? gz / chunkGridSize : (gz - chunkGridSize + 1) / chunkGridSize;

  if (gcx == chunkX && gcz == chunkZ) {
    int32_t localX = gx - gcx * chunkGridSize + 1;
    int32_t localZ = gz - gcz * chunkGridSize + 1;
    const int32_t sampleXCount = chunkGridSize + 2;
    const int32_t sampleZCount = chunkGridSize + 2;
    localX = std::clamp(localX, 0, sampleXCount - 1);
    localZ = std::clamp(localZ, 0, sampleZCount - 1);
    const size_t idx = (static_cast<size_t>(gy) * sampleZCount + localZ) * sampleXCount + localX;
    return densityBuffer[idx];
  }

  float worldX = static_cast<float>(gx) * gridSpacing;
  float worldY = static_cast<float>(gy) * gridSpacing;
  float worldZ = static_cast<float>(gz) * gridSpacing;
  return pipeline.sampleDensity(worldX, worldY, worldZ);
}

/// Replay a single terrain brush edit on a chunk slot's density buffer.
void replayBrushEdit(int32_t chunkX, int32_t chunkZ, ChunkSlot& slot,
                     const WorldGenPipeline& pipeline, const TerrainBrush& brush,
                     int32_t chunkSize, int32_t worldHeight,
                     float gridSpacing) {
  if (!intersectsChunk(chunkX, chunkZ, chunkSize, worldHeight, chunkSize, brush.center, brush.radius)) {
    return;
  }

  int32_t scale = static_cast<int32_t>(1.0f / gridSpacing);
  const int32_t sx = chunkSize * scale;
  const int32_t sy = worldHeight * scale;
  const int32_t sz = chunkSize * scale;
  const int32_t sampleXCount = sx + 2;
  const int32_t sampleZCount = sz + 2;

  struct DensityWrite {
    size_t index;
    float value;
  };
  std::vector<DensityWrite> smoothWrites;

  for (int32_t y = 0; y <= sy; ++y) {
    const float worldY = static_cast<float>(y) * gridSpacing;
    for (int32_t z = -1; z <= sz; ++z) {
      const float worldZ = static_cast<float>(chunkZ * chunkSize) + static_cast<float>(z) * gridSpacing;
      for (int32_t x = -1; x <= sx; ++x) {
        const float worldX = static_cast<float>(chunkX * chunkSize) + static_cast<float>(x) * gridSpacing;

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
            const float self = slot.density[idx];
            float sum = 0.0f;
            sum += getDensityAtReplay(x - 1, y, z, chunkX, chunkZ, slot.density, pipeline, chunkSize, worldHeight, gridSpacing);
            sum += getDensityAtReplay(x + 1, y, z, chunkX, chunkZ, slot.density, pipeline, chunkSize, worldHeight, gridSpacing);
            sum += getDensityAtReplay(x, y - 1, z, chunkX, chunkZ, slot.density, pipeline, chunkSize, worldHeight, gridSpacing);
            sum += getDensityAtReplay(x, y + 1, z, chunkX, chunkZ, slot.density, pipeline, chunkSize, worldHeight, gridSpacing);
            sum += getDensityAtReplay(x, y, z - 1, chunkX, chunkZ, slot.density, pipeline, chunkSize, worldHeight, gridSpacing);
            sum += getDensityAtReplay(x, y, z + 1, chunkX, chunkZ, slot.density, pipeline, chunkSize, worldHeight, gridSpacing);
            const float avg = sum / 6.0f;

            const float falloff = 1.0f - (dist / brush.radius);
            const float blend = std::clamp(falloff * brush.strength, 0.0f, 1.0f);
            const float newValue = (1.0f - blend) * self + blend * avg;
            smoothWrites.push_back({idx, std::clamp(newValue, -500.0f, 500.0f)});
          }
        }
      }
    }
  }

  if (brush.type == BrushType::Smooth) {
    for (const auto& write : smoothWrites) {
      slot.density[write.index] = write.value;
    }
  }
}

/// Replay all terrain edits from history on a chunk slot's density buffer.
void replayAllTerrainEdits(int32_t chunkX, int32_t chunkZ, ChunkSlot& slot,
                           const WorldGenPipeline& pipeline, const std::vector<TerrainEdit>& edits,
                           int32_t chunkSize, int32_t worldHeight,
                           float gridSpacing) {
  for (const auto& edit : edits) {
    replayBrushEdit(chunkX, chunkZ, slot, pipeline, edit.brush, chunkSize, worldHeight, gridSpacing);
  }
}

} // namespace

ChunkWorkerImpl::ChunkWorkerImpl(WorkerThreadPool& genPool, WorkerThreadPool& meshPool,
                                 SharedPool& pool, WorldGenPipeline& pipeline,
                                 const GameConfig& config, WorldController& controller,
                                 ChunkMeshAllocator& meshAllocator)
  : m_genPool(genPool), m_meshPool(meshPool), m_pool(pool),
    m_pipeline(pipeline), m_config(config),
    m_controller(controller), m_meshAllocator(meshAllocator) {}

void ChunkWorkerImpl::generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t /*seed*/) {
  m_genPool.submitAndForget([this, slotIndex, chunkX, chunkZ]() {
    auto slot = m_pool.view(slotIndex);

    // Initialize density buffer
    int32_t scale = static_cast<int32_t>(1.0f / m_config.gridSpacing);
    const int32_t sx = m_config.chunkSize * scale;
    const int32_t sy = m_config.worldHeight * scale;
    const int32_t sz = m_config.chunkSize * scale;
    const int32_t sampleXCount = sx + 2;
    const int32_t sampleYCount = sy + 1;
    const int32_t sampleZCount = sz + 2;
    const int32_t sampleMinX = -1;
    const int32_t sampleMinZ = -1;

    for (int32_t y = 0; y < sampleYCount; ++y) {
      const float worldY = static_cast<float>(y) * m_config.gridSpacing;
      for (int32_t z = 0; z < sampleZCount; ++z) {
        const float worldZ = static_cast<float>(chunkZ * m_config.chunkSize) + static_cast<float>(sampleMinZ + z) * m_config.gridSpacing;
        for (int32_t x = 0; x < sampleXCount; ++x) {
          const float worldX = static_cast<float>(chunkX * m_config.chunkSize) + static_cast<float>(sampleMinX + x) * m_config.gridSpacing;
          const size_t idx = (static_cast<size_t>(y) * sampleZCount + z) * sampleXCount + x;
          slot.density[idx] = m_pipeline.sampleDensity(worldX, worldY, worldZ);
        }
      }
    }
    *slot.densityInitialized = 1;

    // Replay all manual terrain edits on the generated density field
    const auto edits = m_controller.world().editHistory().getEdits();
    replayAllTerrainEdits(chunkX, chunkZ, slot, m_pipeline, edits, m_config.chunkSize, m_config.worldHeight, m_config.gridSpacing);

    *slot.status = static_cast<int32_t>(ChunkSlotStatus::DENSITY_READY);
    m_controller.onGenCompleted(slotIndex);
  });
}

void ChunkWorkerImpl::mesh(int32_t slotIndex) {
  m_meshPool.submitAndForget([this, slotIndex]() {
    auto slot = m_pool.view(slotIndex);
    *slot.vertexCount = 0u;
    *slot.indexCount = 0u;
    *slot.opaqueIndexCount = 0u;
    *slot.transparentIndexCount = 0u;
    *slot.renderFlags = 0u;

    const int32_t chunkX = *slot.chunkX;
    const int32_t chunkZ = *slot.chunkZ;

    const size_t scratchVertexFloats =
        static_cast<size_t>(std::max(0, m_config.maxVertsPerChunk)) *
        static_cast<size_t>(std::max(1, m_config.vertexStrideFloats));
    const size_t scratchIndices = static_cast<size_t>(std::max(0, m_config.maxIndicesPerChunk));
    if (g_meshScratch.vertices.size() < scratchVertexFloats) {
      g_meshScratch.vertices.resize(scratchVertexFloats);
    }
    if (g_meshScratch.indices.size() < scratchIndices) {
      g_meshScratch.indices.resize(scratchIndices);
    }

    mesh::SurfaceNetsConfig scfg;
    int32_t scale = static_cast<int32_t>(1.0f / m_config.gridSpacing);
    scfg.sizeX = m_config.chunkSize * scale;
    scfg.sizeY = m_config.worldHeight * scale;
    scfg.sizeZ = m_config.chunkSize * scale;
    scfg.maxVertices = m_config.maxVertsPerChunk;
    scfg.maxIndices = m_config.maxIndicesPerChunk;
    scfg.strideFloats = m_config.vertexStrideFloats;
    scfg.originX = static_cast<float>(chunkX * m_config.chunkSize);
    scfg.originY = 0.0f;
    scfg.originZ = static_cast<float>(chunkZ * m_config.chunkSize);
    scfg.gridSpacing = m_config.gridSpacing;

    // Ensure density buffer is initialized (e.g. if loaded from disk)
    if (*slot.densityInitialized == 0) {
      const int32_t sx = m_config.chunkSize * scale;
      const int32_t sy = m_config.worldHeight * scale;
      const int32_t sz = m_config.chunkSize * scale;
      const int32_t sampleXCount = sx + 2;
      const int32_t sampleYCount = sy + 1;
      const int32_t sampleZCount = sz + 2;
      const int32_t sampleMinX = -1;
      const int32_t sampleMinZ = -1;

      for (int32_t y = 0; y < sampleYCount; ++y) {
        const float worldY = static_cast<float>(y) * m_config.gridSpacing;
        for (int32_t z = 0; z < sampleZCount; ++z) {
          const float worldZ = static_cast<float>(chunkZ * m_config.chunkSize) + static_cast<float>(sampleMinZ + z) * m_config.gridSpacing;
          for (int32_t x = 0; x < sampleXCount; ++x) {
            const float worldX = static_cast<float>(chunkX * m_config.chunkSize) + static_cast<float>(sampleMinX + x) * m_config.gridSpacing;
            const size_t idx = (static_cast<size_t>(y) * sampleZCount + z) * sampleXCount + x;
            slot.density[idx] = m_pipeline.sampleDensity(worldX, worldY, worldZ);
          }
        }
      }
      *slot.densityInitialized = 1;

      // Replay all manual terrain edits on the generated density field
      const auto edits = m_controller.world().editHistory().getEdits();
      replayAllTerrainEdits(chunkX, chunkZ, slot, m_pipeline, edits, m_config.chunkSize, m_config.worldHeight, m_config.gridSpacing);
    }

    DensitySlotContext dctx{};
    dctx.densityBuffer = slot.density;
    dctx.chunkX = chunkX;
    dctx.chunkZ = chunkZ;
    dctx.sizeX = m_config.chunkSize * scale;
    dctx.sizeY = m_config.worldHeight * scale;
    dctx.sizeZ = m_config.chunkSize * scale;
    dctx.gridSpacing = m_config.gridSpacing;

    mesh::DensitySampler sampler{};
    sampler.userData = &dctx;
    sampler.sample = &slotDensitySample;

    uint32_t vc = 0, ic = 0;
    bool ok = mesh::surfaceNetsMesh(
        scfg, sampler,
        g_meshScratch.vertices.data(),
        g_meshScratch.indices.data(),
        vc, ic);

    if (ok) {
      annotateSurfaceNetsVertices(m_pipeline, scfg,
                                  g_meshScratch.vertices.data(), vc);
      *slot.renderFlags |= CHUNK_RENDER_FLAG_TERRAIN;
    }

    if (!ok) {
      std::cerr << "Chunk mesh build failed for (" << chunkX << ", " << chunkZ << ")\n";
      m_meshAllocator.releaseSlot(slotIndex);
      *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
      m_controller.onMeshCompleted(slotIndex, false);
      return;
    }

    if (vc == 0u || ic == 0u) {
      m_meshAllocator.releaseSlot(slotIndex);
      *slot.renderFlags = 0u;
      *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
      m_controller.onMeshCompleted(slotIndex, true);
      return;
    }

    auto allocation = m_meshAllocator.allocateForSlot(
        slotIndex,
        static_cast<size_t>(vc) * m_config.vertexStrideFloats * sizeof(float),
        static_cast<size_t>(ic) * sizeof(uint32_t));

    if (!allocation || !allocation->valid()) {
      std::cerr << "Chunk mesh allocation failed for (" << chunkX << ", " << chunkZ
                << ") requesting " << vc << " verts / " << ic << " indices\n";
      *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
      m_controller.onMeshCompleted(slotIndex, false);
      return;
    }

    std::memcpy(allocation->vboPtr, g_meshScratch.vertices.data(),
                static_cast<size_t>(vc) * m_config.vertexStrideFloats * sizeof(float));
    std::memcpy(allocation->iboPtr, g_meshScratch.indices.data(),
                static_cast<size_t>(ic) * sizeof(uint32_t));

    std::shared_ptr<TerrainChunkCollision> terrainCollision = nullptr;
    if (ok && (*slot.renderFlags & CHUNK_RENDER_FLAG_TERRAIN) != 0u) {
      std::vector<TerrainTriangle> triangles;
      triangles.reserve(ic / 3);
      float originX = static_cast<float>(chunkX * m_config.chunkSize);
      float originY = 0.0f;
      float originZ = static_cast<float>(chunkZ * m_config.chunkSize);
      size_t stride = static_cast<size_t>(m_config.vertexStrideFloats);

      for (size_t i = 0; i < ic; i += 3) {
        uint32_t i0 = g_meshScratch.indices[i];
        uint32_t i1 = g_meshScratch.indices[i + 1];
        uint32_t i2 = g_meshScratch.indices[i + 2];

        glm::vec3 v0(
          originX + g_meshScratch.vertices[i0 * stride + 0],
          originY + g_meshScratch.vertices[i0 * stride + 1],
          originZ + g_meshScratch.vertices[i0 * stride + 2]
        );
        glm::vec3 v1(
          originX + g_meshScratch.vertices[i1 * stride + 0],
          originY + g_meshScratch.vertices[i1 * stride + 1],
          originZ + g_meshScratch.vertices[i1 * stride + 2]
        );
        glm::vec3 v2(
          originX + g_meshScratch.vertices[i2 * stride + 0],
          originY + g_meshScratch.vertices[i2 * stride + 1],
          originZ + g_meshScratch.vertices[i2 * stride + 2]
        );

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 normal = glm::cross(edge1, edge2);
        float len = glm::length(normal);
        if (len > 1e-6f) {
          normal /= len;
          triangles.push_back({v0, v1, v2, normal});
        }
      }

      terrainCollision = std::make_shared<TerrainChunkCollision>();
      terrainCollision->build(std::move(triangles));
    }

    *slot.vertexCount = static_cast<uint32_t>(vc);
    *slot.indexCount = ic;
    *slot.opaqueIndexCount = ic;
    *slot.transparentIndexCount = 0u;
    *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
    if (ok && (*slot.renderFlags & CHUNK_RENDER_FLAG_TERRAIN) != 0u) {
      *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_OPAQUE;
    }
    m_controller.onMeshCompleted(slotIndex, true, std::move(terrainCollision));
  });
}

void ChunkWorkerImpl::setGpuTargets(float* vboPtr, size_t vboMaxBytes,
                                    uint32_t* iboPtr, size_t iboMaxBytes) {
  (void)vboPtr;
  (void)vboMaxBytes;
  (void)iboPtr;
  (void)iboMaxBytes;
}

} // namespace terrain
