#pragma once

#include <string>
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

/// Represents one chunk in the world.
struct Chunk {
  int32_t chunkX;
  int32_t chunkZ;
  int32_t slotIndex;
  ChunkState state = ChunkState::QueuedGen;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  bool needsRemesh = false;

  [[nodiscard]] auto key() const -> std::string {
    return std::to_string(chunkX) + ":" + std::to_string(chunkZ);
  }

  static auto makeKey(int32_t cx, int32_t cz) -> std::string {
    return std::to_string(cx) + ":" + std::to_string(cz);
  }
};

} // namespace voxel
