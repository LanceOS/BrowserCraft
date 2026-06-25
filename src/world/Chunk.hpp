#pragma once

#include <cstdint>

namespace voxel {

/// Lifecycle state of a chunk.
enum class ChunkState {
  LoadingFromDisk,
  QueuedGen,
  Generating,
  VoxelsReady,
  QueuedMesh,
  Meshing,
  MeshReady,
  Uploaded,
  MeshFailed,
};

/// Pack chunk coordinates into a single 64-bit key.
inline auto chunkKey(int32_t cx, int32_t cz) -> int64_t {
  return (static_cast<int64_t>(cx) << 32) | (static_cast<uint32_t>(cz));
}

/// Deterministic chunk seed for world generation.
inline auto chunkSeed(int32_t chunkX, int32_t chunkZ, uint32_t seed) -> uint32_t {
  uint32_t h = seed ^ static_cast<uint32_t>(chunkX) * 374761393u
                      ^ static_cast<uint32_t>(chunkZ) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h;
}

/// Maximum number of times to retry chunk generation before giving up.
inline constexpr int32_t MAX_CHUNK_GEN_RETRIES = 3;

/// Represents one chunk in the world.
struct Chunk {
  int32_t chunkX;
  int32_t chunkZ;
  int32_t slotIndex;
  ChunkState state = ChunkState::QueuedGen;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  bool needsRemesh = false;
  bool hasTransparent = false; // whether mesh contains alpha-blended geometry
  int32_t genRetries = 0; // number of times generation has been attempted

  [[nodiscard]] auto key() const -> int64_t {
    return chunkKey(chunkX, chunkZ);
  }

  static auto makeKey(int32_t cx, int32_t cz) -> int64_t {
    return chunkKey(cx, cz);
  }
};

} // namespace voxel
