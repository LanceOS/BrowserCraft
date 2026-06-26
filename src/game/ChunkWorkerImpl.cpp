#include "game/ChunkWorkerImpl.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/render/ChunkMeshAllocator.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "engine/workers/mesher/GreedyMesher.hpp"
#include "engine/workers/mesher/LightPropagator.hpp"
#include "engine/workers/mesher/LightSampling.hpp"
#include "game/WorldController.hpp"
#include "world/BlockIds.hpp"
#include "world/BlockRegistry.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include "world/mesh/SurfaceNetsMesher.hpp"
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

struct TerrainTexturePalette {
  uint16_t grassTop = 0;
  uint16_t grassBottom = 0;
  uint16_t grassSide = 0;
  uint16_t dirt = 0;
  uint16_t stone = 0;
  uint16_t sand = 0;
};

auto loadTerrainPalette(const BlockRegistry& blocks) -> TerrainTexturePalette {
  TerrainTexturePalette palette{};

  if (const auto* grass = blocks.tryGet(BlockId::GRASS)) {
    palette.grassTop = grass->textures.top;
    palette.grassBottom = grass->textures.bottom;
    palette.grassSide = grass->textures.side;
  }
  if (const auto* dirt = blocks.tryGet(BlockId::DIRT)) {
    palette.dirt = dirt->textures.top;
  }
  if (const auto* stone = blocks.tryGet(BlockId::STONE)) {
    palette.stone = stone->textures.top;
  }
  if (const auto* sand = blocks.tryGet(BlockId::SAND)) {
    palette.sand = sand->textures.top;
  }

  return palette;
}

auto pickTerrainLayer(MaterialId material, float nx, float ny, float nz,
                      const TerrainTexturePalette& palette) -> uint16_t {
  const float ax = std::fabs(nx);
  const float ay = std::fabs(ny);
  const float az = std::fabs(nz);

  switch (material) {
    case MaterialId::Grass:
      if (ay >= ax && ay >= az) {
        return ny >= 0.0f ? palette.grassTop : palette.grassBottom;
      }
      return palette.grassSide;
    case MaterialId::Dirt:
      return palette.dirt != 0u ? palette.dirt : palette.stone;
    case MaterialId::Sand:
      return palette.sand != 0u ? palette.sand : palette.dirt;
    case MaterialId::Stone:
    default:
      return palette.stone != 0u ? palette.stone : palette.dirt;
  }
}

auto surfaceDensitySample(void* userData, float worldX, float worldY, float worldZ) -> float {
  auto* pipeline = static_cast<WorldGenPipeline*>(userData);
  return pipeline->sampleDensity(worldX, worldY, worldZ);
}

void annotateSurfaceNetsVertices(const WorldGenPipeline& pipeline,
                                 const TerrainTexturePalette& palette,
                                 const mesh::SurfaceNetsConfig& cfg,
                                 float* vertexOut,
                                 uint32_t vertexCount) {
  if (!vertexOut) return;

  for (uint32_t i = 0; i < vertexCount; ++i) {
    float* v = vertexOut + static_cast<size_t>(i) * static_cast<size_t>(cfg.strideFloats);
    const float worldX = cfg.originX + v[0];
    const float worldY = cfg.originY + v[1];
    const float worldZ = cfg.originZ + v[2];
    const MaterialId material = pipeline.sampleMaterial(worldX, worldY, worldZ);
    const uint16_t texLayer = pickTerrainLayer(material, v[3], v[4], v[5], palette);
    v[8] = static_cast<float>(texLayer);
    v[9] = mesher::packLight(15, 0, 0);
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

    mesh::DensitySampler sampler{};
    sampler.userData = &m_pipeline;
    sampler.sample = &surfaceDensitySample;

    bool ok = mesh::surfaceNetsMesh(
        scfg, sampler,
        g_meshScratch.vertices.data(),
        g_meshScratch.indices.data(),
        vc, ic);
    if (ok) {
      annotateSurfaceNetsVertices(m_pipeline, loadTerrainPalette(m_blocks), scfg,
                                  g_meshScratch.vertices.data(), vc);
      opaqueIc = ic;
      transparentIc = 0u;
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

    *slot.vertexCount = static_cast<uint32_t>(vc);
    *slot.indexCount = ic;
    *slot.opaqueIndexCount = opaqueIc;
    *slot.transparentIndexCount = transparentIc;
    *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
    if (transparentIc > 0u) *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_TRANSPARENT;
    if (opaqueIc > 0u) *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_OPAQUE;
    m_controller.onMeshCompleted(slotIndex, true);
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
