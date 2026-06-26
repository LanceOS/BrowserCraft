#pragma once

#include <cstdint>

// @deprecated Legacy terrain-world code retained during the render-only migration to triangle meshes.
namespace terrain {

/// Shared chunk-slot lifecycle state used by the pool, job queue, world, and
/// renderer.
enum class ChunkSlotStatus : int32_t {
  FREE = 0,
  GENERATING = 1,
  DENSITY_READY = 2,
  MESHING = 3,
  MESH_READY = 4,
  GPU_UPLOADED = 5,
};

/// Render metadata written by the mesher and consumed by the render path.
/// Kept separate from the lifecycle status word so allocation code can stay
/// unaware of render-specific state.
inline constexpr uint32_t CHUNK_RENDER_FLAG_HAS_TRANSPARENT = 1u << 0;
inline constexpr uint32_t CHUNK_RENDER_FLAG_HAS_OPAQUE = 1u << 1;
inline constexpr uint32_t CHUNK_RENDER_FLAG_TERRAIN = 1u << 2;

/// Canonical chunk sizing metadata used when constructing the shared pool.
struct ChunkDimensions {
  int32_t sizeX;
  int32_t sizeY;
  int32_t sizeZ;
  int32_t maxVertsPerChunk;
  int32_t maxIndicesPerChunk;
  int32_t vertexStrideFloats;
};

} // namespace terrain
