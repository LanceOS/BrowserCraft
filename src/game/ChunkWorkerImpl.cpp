#include "game/ChunkWorkerImpl.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/render/ChunkMeshAllocator.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "engine/workers/mesher/GreedyMesher.hpp"
#include "engine/workers/mesher/LightPropagator.hpp"
#include "game/WorldController.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockIds.hpp"
#include "world/mesh/SurfaceNetsMesher.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace voxel {

namespace {

inline auto slotHasVoxelData(int32_t status) -> bool {
  return status >= static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
}

struct MeshScratchBuffers {
  std::vector<float> vertices;
  std::vector<uint32_t> indices;
};

struct TerrainTextureLayers {
  uint16_t grassTop = 0;
  uint16_t grassSide = 0;
  uint16_t dirt = 0;
  uint16_t stone = 0;
  uint16_t sand = 0;
};

thread_local MeshScratchBuffers g_meshScratch;

auto samplePipelineDensity(void* userData, float worldX, float worldY, float worldZ) -> float {
  return static_cast<WorldGenPipeline*>(userData)->sampleDensity(worldX, worldY, worldZ);
}

auto resolveTerrainTextureLayers(const BlockRegistry& blocks) -> TerrainTextureLayers {
  TerrainTextureLayers layers{};

  if (const auto* grass = blocks.tryGet(BlockId::GRASS)) {
    layers.grassTop = grass->textures.top;
    layers.grassSide = grass->textures.side;
  }
  if (const auto* dirt = blocks.tryGet(BlockId::DIRT)) {
    layers.dirt = dirt->textures.top;
  }
  if (const auto* stone = blocks.tryGet(BlockId::STONE)) {
    layers.stone = stone->textures.top;
  }
  if (const auto* sand = blocks.tryGet(BlockId::SAND)) {
    layers.sand = sand->textures.top;
  }

  return layers;
}

void applyTerrainTextureLayers(float* vertices, uint32_t vertexCount, uint32_t strideFloats,
                               const GameConfig& config, int32_t chunkX, int32_t chunkZ,
                               WorldGenPipeline& pipeline, const TerrainTextureLayers& layers) {
  if (!vertices) return;
  const float originX = static_cast<float>(chunkX * config.chunkSize);
  const float originZ = static_cast<float>(chunkZ * config.chunkSize);

  for (uint32_t i = 0; i < vertexCount; ++i) {
    float* v = vertices + static_cast<size_t>(i) * strideFloats;
    const float worldX = originX + v[0];
    const float worldY = v[1];
    const float worldZ = originZ + v[2];
    const float normalY = v[4];

    const auto material = pipeline.sampleMaterial(worldX, worldY, worldZ);
    uint16_t layer = layers.stone;
    switch (material) {
      case MaterialId::Grass:
        layer = normalY > 0.55f ? layers.grassTop : layers.grassSide;
        break;
      case MaterialId::Dirt:
        layer = layers.dirt;
        break;
      case MaterialId::Stone:
        layer = layers.stone;
        break;
      case MaterialId::Sand:
        layer = layers.sand != 0u ? layers.sand : layers.dirt;
        break;
      default:
        layer = layers.stone;
        break;
    }
    v[8] = static_cast<float>(layer);
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

    const int32_t chunkX = *slot.chunkX;
    const int32_t chunkZ = *slot.chunkZ;
    const uint32_t strideFloats = static_cast<uint32_t>(m_config.vertexStrideFloats);

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

    uint32_t vc = 0, ic = 0;
    uint32_t opaqueIc = 0, transparentIc = 0;
    bool hasTransparent = false;
    bool hasOpaque = false;
    bool ok = false;

    if (m_config.useSurfaceNets) {
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

      mesh::DensitySampler densitySampler{};
      densitySampler.userData = &m_pipeline;
      densitySampler.sample = &samplePipelineDensity;

      ok = mesh::surfaceNetsMesh(
          scfg, densitySampler,
          g_meshScratch.vertices.data(),
          g_meshScratch.indices.data(),
          vc, ic);

      if (!ok) {
        std::cerr << "Surface Nets mesh build exceeded scratch limits for (" << chunkX << ", "
                  << chunkZ << ") max " << scfg.maxVertices << " verts / "
                  << scfg.maxIndices << " indices\n";
        m_meshAllocator.releaseSlot(slotIndex);
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
        m_controller.onMeshCompleted(slotIndex, false);
        return;
      }

      if (vc > 0u) {
        const auto terrainLayers = resolveTerrainTextureLayers(m_blocks);
        applyTerrainTextureLayers(g_meshScratch.vertices.data(), vc, strideFloats,
                                  m_config, chunkX, chunkZ, m_pipeline, terrainLayers);
        hasOpaque = true;
        opaqueIc = ic;
      }
    } else {
      mesher::MesherConfig mcfg;
      mcfg.sizeX = m_config.chunkSize;
      mcfg.sizeY = m_config.worldHeight;
      mcfg.sizeZ = m_config.chunkSize;
      mcfg.maxVertices = m_config.maxVertsPerChunk;
      mcfg.maxIndices = m_config.maxIndicesPerChunk;
      mcfg.strideFloats = m_config.vertexStrideFloats;

      const auto neighbors = gatherNeighborVoxels(slotIndex, chunkX, chunkZ);

      // Rebuild the packed light volume for every remesh so terrain edits and
      // streamed-in chunks keep their smooth lighting in sync with geometry.
      mesher::calculateLighting(slot.voxels, slot.light, m_blocks, mcfg);

      ok = mesher::greedyMesh(
          slot.voxels, slot.light, m_blocks, mcfg,
          g_meshScratch.vertices.data(),
          g_meshScratch.indices.data(),
          vc, ic, &hasTransparent, &hasOpaque, &opaqueIc, &transparentIc, neighbors);
      if (!ok) {
        std::cerr << "Chunk mesh build exceeded scratch limits for (" << chunkX << ", " << chunkZ
                  << ") max " << mcfg.maxVertices << " verts / "
                  << mcfg.maxIndices << " indices\n";
        m_meshAllocator.releaseSlot(slotIndex);
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
        m_controller.onMeshCompleted(slotIndex, false);
        return;
      }
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
      std::cerr << "Chunk mesh allocation failed for (" << chunkX << ", " << chunkZ
                << ") requesting " << vc << " verts / "
                << ic << " indices\n";
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
    if (hasTransparent) *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_TRANSPARENT;
    if (hasOpaque) *slot.renderFlags |= CHUNK_RENDER_FLAG_HAS_OPAQUE;
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

auto ChunkWorkerImpl::gatherNeighborVoxels(int32_t slotIndex, int32_t chunkX, int32_t chunkZ) const
    -> mesher::NeighborVoxelViews {
  mesher::NeighborVoxelViews neighbors{};

  for (int32_t i = 0; i < m_pool.capacity(); ++i) {
    if (i == slotIndex) continue;

    auto candidate = m_pool.view(i);
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

} // namespace voxel
