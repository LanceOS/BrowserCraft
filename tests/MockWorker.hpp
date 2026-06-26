#pragma once

#include "world/IChunkWorker.hpp"
#include "world/IChunkPersistence.hpp"
#include <functional>

namespace terrain {

/// Minimal IChunkWorker for unit tests — captures gen/mesh lambdas.
class TestChunkWorker final : public IChunkWorker {
public:
  using GenFn = std::function<void(int32_t, int32_t, int32_t, uint32_t)>;
  using MeshFn = std::function<void(int32_t)>;

  TestChunkWorker(GenFn genFn = {}, MeshFn meshFn = {})
    : m_genFn(std::move(genFn)), m_meshFn(std::move(meshFn)) {}

  void generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed) override {
    if (m_genFn) m_genFn(slotIndex, chunkX, chunkZ, seed);
  }

  void mesh(int32_t slotIndex) override {
    if (m_meshFn) m_meshFn(slotIndex);
  }

  void setGpuTargets(float*, size_t, uint32_t*, size_t) override {
    // No-op
  }

private:
  GenFn m_genFn;
  MeshFn m_meshFn;
};

/// No-op IChunkPersistence for tests that don't need save/load.
class NullPersistence final : public IChunkPersistence {
public:
  void requestLoad(int32_t, int32_t) override {}
  void markDirty(int32_t, int32_t) override {}
  void recordTerrainEdit(const TerrainBrush&) override {}
};

} // namespace terrain
