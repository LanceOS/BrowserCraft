#include "game/ChunkWorkerImpl.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/render/ChunkMeshAllocator.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "engine/workers/mesher/GreedyMesher.hpp"
#include "engine/workers/mesher/LightPropagator.hpp"
#include "engine/workers/mesher/LightSampling.hpp"
#include "game/WorldController.hpp"
#include "world/BlockRegistry.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include "world/mesh/SurfaceNetsMesher.hpp"
#include "world/terrain/TerrainCollision.hpp"
#include "world/ChunkCoords.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

namespace {

auto slotHasVoxelData(int32_t status) -> bool {
  return status >= static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
}

struct MeshScratchBuffers {
  std::vector<float> vertices;
  std::vector<uint32_t> indices;
};

thread_local MeshScratchBuffers g_meshScratch;

auto gatherNeighborVoxels(const SharedPool& pool, int32_t slotIndex,
                          int32_t chunkX, int32_t chunkZ) -> mesher::NeighborVoxelViews {
  mesher::NeighborVoxelViews neighbors{};

  for (int32_t i = 0; i < pool.capacity(); ++i) {
    if (i == slotIndex) continue;

    auto candidate = pool.view(i);
    if (!slotHasVoxelData(*candidate.status)) continue;

    const int32_t cx = *candidate.chunkX;
    const int32_t cz = *candidate.chunkZ;
    if (cx == chunkX + 1 && cz == chunkZ) {
      neighbors.px = candidate.voxels;
    } else if (cx == chunkX - 1 && cz == chunkZ) {
      neighbors.nx = candidate.voxels;
    } else if (cx == chunkX && cz == chunkZ + 1) {
      neighbors.pz = candidate.voxels;
    } else if (cx == chunkX && cz == chunkZ - 1) {
      neighbors.nz = candidate.voxels;
    }

    if (neighbors.px && neighbors.nx && neighbors.pz && neighbors.nz) {
      break;
    }
  }

  return neighbors;
}

auto surfaceDensitySample(void* userData, float worldX, float worldY, float worldZ) -> float {
  auto* pipeline = static_cast<WorldGenPipeline*>(userData);
  return pipeline->sampleDensity(worldX, worldY, worldZ);
}

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
};

auto slotDensitySample(void* userData, float worldX, float worldY, float worldZ) -> float {
  auto* ctx = static_cast<DensitySlotContext*>(userData);
  const int32_t sx = ctx->sizeX;
  const int32_t sy = ctx->sizeY;
  const int32_t sz = ctx->sizeZ;
  const int32_t sampleXCount = sx + 2;
  const int32_t sampleZCount = sz + 2;

  const int32_t localX = static_cast<int32_t>(std::round(worldX)) - ctx->chunkX * sx + 1;
  const int32_t localY = static_cast<int32_t>(std::round(worldY));
  const int32_t localZ = static_cast<int32_t>(std::round(worldZ)) - ctx->chunkZ * sz + 1;

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
auto getDensityAtReplay(int32_t wx, int32_t wy, int32_t wz,
                        int32_t chunkX, int32_t chunkZ,
                        const float* densityBuffer,
                        const WorldGenPipeline& pipeline,
                        int32_t chunkSize, int32_t worldHeight) -> float {
  if (wy < 0 || wy > worldHeight) {
    return wy < 0 ? -1.0f : 1.0f;
  }

  const int32_t cx = floorToChunk(wx, chunkSize);
  const int32_t cz = floorToChunk(wz, chunkSize);

  if (cx == chunkX && cz == chunkZ) {
    int32_t localX = wx - cx * chunkSize + 1;
    int32_t localZ = wz - cz * chunkSize + 1;
    const int32_t sampleXCount = chunkSize + 2;
    const int32_t sampleZCount = chunkSize + 2;
    localX = std::clamp(localX, 0, sampleXCount - 1);
    localZ = std::clamp(localZ, 0, sampleZCount - 1);
    const size_t idx = (static_cast<size_t>(wy) * sampleZCount + localZ) * sampleXCount + localX;
    return densityBuffer[idx];
  }

  return pipeline.sampleDensity(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz));
}

/// Replay a single terrain brush edit on a chunk slot's density buffer.
void replayBrushEdit(int32_t chunkX, int32_t chunkZ, ChunkSlot& slot,
                     const WorldGenPipeline& pipeline, const TerrainBrush& brush,
                     int32_t chunkSize, int32_t worldHeight) {
  if (!intersectsChunk(chunkX, chunkZ, chunkSize, worldHeight, chunkSize, brush.center, brush.radius)) {
    return;
  }

  const int32_t sx = chunkSize;
  const int32_t sy = worldHeight;
  const int32_t sz = chunkSize;
  const int32_t sampleXCount = sx + 2;
  const int32_t sampleZCount = sz + 2;

  struct DensityWrite {
    size_t index;
    float value;
  };
  std::vector<DensityWrite> smoothWrites;

  for (int32_t y = 0; y <= sy; ++y) {
    const float worldY = static_cast<float>(y);
    for (int32_t z = -1; z <= sz; ++z) {
      const float worldZ = static_cast<float>(chunkZ * sz + z);
      for (int32_t x = -1; x <= sx; ++x) {
        const float worldX = static_cast<float>(chunkX * sx + x);

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
            sum += getDensityAtReplay(wx - 1, wy, wz, chunkX, chunkZ, slot.density, pipeline, sx, sy);
            sum += getDensityAtReplay(wx + 1, wy, wz, chunkX, chunkZ, slot.density, pipeline, sx, sy);
            sum += getDensityAtReplay(wx, wy - 1, wz, chunkX, chunkZ, slot.density, pipeline, sx, sy);
            sum += getDensityAtReplay(wx, wy + 1, wz, chunkX, chunkZ, slot.density, pipeline, sx, sy);
            sum += getDensityAtReplay(wx, wy, wz - 1, chunkX, chunkZ, slot.density, pipeline, sx, sy);
            sum += getDensityAtReplay(wx, wy, wz + 1, chunkX, chunkZ, slot.density, pipeline, sx, sy);
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
                           int32_t chunkSize, int32_t worldHeight) {
  for (const auto& edit : edits) {
    replayBrushEdit(chunkX, chunkZ, slot, pipeline, edit.brush, chunkSize, worldHeight);
  }
}

} // namespace

ChunkWorkerImpl::ChunkWorkerImpl(WorkerThreadPool& genPool, WorkerThreadPool& meshPool,
                                 SharedPool& pool, WorldGenPipeline& pipeline,
                                 const GameConfig& config, WorldController& controller,
                                 BlockRegistry& blocks, ChunkMeshAllocator& meshAllocator)
  : m_genPool(genPool), m_meshPool(meshPool), m_pool(pool),
    m_pipeline(pipeline), m_config(config),
    m_controller(controller), m_blocks(blocks),
    m_meshAllocator(meshAllocator) {}

void ChunkWorkerImpl::generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed) {
  m_genPool.submitAndForget([this, slotIndex, chunkX, chunkZ, seed]() {
    auto slot = m_pool.view(slotIndex);
    m_pipeline.generate(slot.voxels, chunkX, chunkZ,
      m_config.chunkSize, m_config.worldHeight, m_config.chunkSize, seed);

    // Initialize density buffer
    const int32_t sx = m_config.chunkSize;
    const int32_t sy = m_config.worldHeight;
    const int32_t sz = m_config.chunkSize;
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
          slot.density[idx] = m_pipeline.sampleDensity(worldX, worldY, worldZ);
        }
      }
    }
    *slot.densityInitialized = 1;

    // Replay all manual terrain edits on the generated density field
    const auto edits = m_controller.world().editHistory().getEdits();
    replayAllTerrainEdits(chunkX, chunkZ, slot, m_pipeline, edits, sx, sy);

    *slot.status = static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
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

    mesher::MesherConfig mcfg;
    mcfg.sizeX = m_config.chunkSize;
    mcfg.sizeY = m_config.worldHeight;
    mcfg.sizeZ = m_config.chunkSize;
    mcfg.maxVertices = m_config.maxVertsPerChunk;
    mcfg.maxIndices = m_config.maxIndicesPerChunk;
    mcfg.strideFloats = m_config.vertexStrideFloats;

    const int32_t chunkX = *slot.chunkX;
    const int32_t chunkZ = *slot.chunkZ;
    const auto neighbors = gatherNeighborVoxels(m_pool, slotIndex, chunkX, chunkZ);
    mesher::calculateLighting(slot.voxels, slot.light, m_blocks, mcfg);

    const size_t scratchVertexFloats =
        static_cast<size_t>(std::max(0, mcfg.maxVertices)) *
        static_cast<size_t>(std::max(1, mcfg.strideFloats));
    const size_t scratchIndices = static_cast<size_t>(std::max(0, mcfg.maxIndices));
    if (g_meshScratch.vertices.size() < scratchVertexFloats) {
      g_meshScratch.vertices.resize(scratchVertexFloats);
    }
    if (g_meshScratch.indices.size() < scratchIndices) {
      g_meshScratch.indices.resize(scratchIndices);
    }

    uint32_t vc = 0, ic = 0;
    uint32_t opaqueIc = 0, transparentIc = 0;
    bool usedGreedyFallback = false;
    const auto buildGreedyMesh = [&]() -> bool {
      return mesher::greedyMesh(
          slot.voxels, slot.light, m_blocks, mcfg,
          g_meshScratch.vertices.data(),
          g_meshScratch.indices.data(),
          vc, ic, nullptr, nullptr, &opaqueIc, &transparentIc, neighbors);
    };

    mesh::SurfaceNetsConfig scfg;
    scfg.sizeX = m_config.chunkSize;
    scfg.sizeY = m_config.worldHeight;
    scfg.sizeZ = m_config.chunkSize;
    scfg.maxVertices = m_config.maxVertsPerChunk;
    scfg.maxIndices = m_config.maxIndicesPerChunk;
    scfg.strideFloats = m_config.vertexStrideFloats;
    scfg.originX = static_cast<float>(chunkX * m_config.chunkSize);
    scfg.originY = 0.0f;
    scfg.originZ = static_cast<float>(chunkZ * m_config.chunkSize);

    // Ensure density buffer is initialized (e.g. if loaded from disk)
    if (*slot.densityInitialized == 0) {
      const int32_t sx = m_config.chunkSize;
      const int32_t sy = m_config.worldHeight;
      const int32_t sz = m_config.chunkSize;
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
            slot.density[idx] = m_pipeline.sampleDensity(worldX, worldY, worldZ);
          }
        }
      }
      *slot.densityInitialized = 1;

      // Replay all manual terrain edits on the generated density field
      const auto edits = m_controller.world().editHistory().getEdits();
      replayAllTerrainEdits(chunkX, chunkZ, slot, m_pipeline, edits, sx, sy);
    }

    DensitySlotContext dctx{};
    dctx.densityBuffer = slot.density;
    dctx.chunkX = chunkX;
    dctx.chunkZ = chunkZ;
    dctx.sizeX = m_config.chunkSize;
    dctx.sizeY = m_config.worldHeight;
    dctx.sizeZ = m_config.chunkSize;

    mesh::DensitySampler sampler{};
    sampler.userData = &dctx;
    sampler.sample = &slotDensitySample;

    bool ok = mesh::surfaceNetsMesh(
        scfg, sampler,
        g_meshScratch.vertices.data(),
        g_meshScratch.indices.data(),
        vc, ic);
    if (ok) {
      annotateSurfaceNetsVertices(m_pipeline, scfg,
                                  g_meshScratch.vertices.data(), vc);
      opaqueIc = ic;
      transparentIc = 0u;
      *slot.renderFlags |= CHUNK_RENDER_FLAG_TERRAIN;
    } else {
      usedGreedyFallback = true;
      ok = buildGreedyMesh();
    }

    if (!ok) {
      std::cerr << "Chunk mesh build failed for (" << chunkX << ", " << chunkZ
                << ")" << (usedGreedyFallback ? " after greedy fallback" : "") << "\n";
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
      if (!usedGreedyFallback) {
        usedGreedyFallback = true;
        *slot.renderFlags &= ~CHUNK_RENDER_FLAG_TERRAIN;
        ok = buildGreedyMesh();
        if (ok) {
          allocation = m_meshAllocator.allocateForSlot(
              slotIndex,
              static_cast<size_t>(vc) * m_config.vertexStrideFloats * sizeof(float),
              static_cast<size_t>(ic) * sizeof(uint32_t));
        }
      }
    }
    if (!allocation || !allocation->valid()) {
      if (!ok) {
        std::cerr << "Chunk mesh build failed for (" << chunkX << ", " << chunkZ
                  << ")" << (usedGreedyFallback ? " after greedy fallback" : "") << "\n";
      } else {
        std::cerr << "Chunk mesh allocation failed for (" << chunkX << ", " << chunkZ
                  << ") requesting " << vc << " verts / " << ic << " indices"
                  << (usedGreedyFallback ? " after greedy fallback" : "") << "\n";
      }
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
    *slot.opaqueIndexCount = opaqueIc;
    *slot.transparentIndexCount = transparentIc;
    *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
    if (ok && (*slot.renderFlags & CHUNK_RENDER_FLAG_TERRAIN) != 0u) {
      *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_OPAQUE;
    }
    if (transparentIc > 0u) *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_TRANSPARENT;
    if (opaqueIc > 0u) *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_OPAQUE;
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

} // namespace voxel
