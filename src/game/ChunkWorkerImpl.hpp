#pragma once

#include "engine/core/Config.hpp"
#include "world/IChunkWorker.hpp"
#include <cstddef>
#include <cstdint>

namespace terrain {

class ChunkMeshAllocator;
class SharedPool;
class WorkerThreadPool;
class WorldController;
class WorldGenPipeline;

class ChunkWorkerImpl final : public IChunkWorker {
public:
  ChunkWorkerImpl(WorkerThreadPool& genPool, WorkerThreadPool& meshPool,
                  SharedPool& pool, WorldGenPipeline& pipeline,
                  const GameConfig& config, WorldController& controller,
                  ChunkMeshAllocator& meshAllocator);

  void generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed) override;
  void mesh(int32_t slotIndex) override;
  void setGpuTargets(float* vboPtr, size_t vboMaxBytes,
                     uint32_t* iboPtr, size_t iboMaxBytes) override;

private:
  WorkerThreadPool& m_genPool;
  WorkerThreadPool& m_meshPool;
  SharedPool& m_pool;
  WorldGenPipeline& m_pipeline;
  const GameConfig& m_config;
  WorldController& m_controller;
  ChunkMeshAllocator& m_meshAllocator;
};

} // namespace terrain
