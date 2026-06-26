#pragma once

#include <cstdint>
#include "world/BlockRegistry.hpp"

namespace voxel {
namespace mesher {

/// Configuration for the greedy mesher.
struct MesherConfig {
  int32_t sizeX = 16;
  int32_t sizeY = 256;
  int32_t sizeZ = 16;
  int32_t maxVertices = 50000;
  int32_t maxIndices = 100000;
  int32_t strideFloats = 10;
};

/// Upper-bound hint for the greedy mesher output.
struct MeshCapacityHint {
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  uint32_t quadCount = 0;
};

/// Estimate the maximum mesh output needed for the current voxel volume.
/// The result is an upper bound, not an exact count.
[[nodiscard]] auto estimateMeshCapacity(
    const uint8_t* voxels,
    const BlockRegistry& blocks,
    const MesherConfig& cfg) -> MeshCapacityHint;

/// Perform greedy meshing on a chunk's voxel data.
///
/// Reads block IDs from \a voxels and light data from \a light,
/// then writes interleaved vertex and index data into \a vertexOut
/// and \a indexOut. \a vertexCountOut and \a indexCountOut receive
/// the number of vertices and indices written.
///
/// Returns false if the vertex or index buffer capacity was exceeded
/// (the mesh is truncated).  Returns true on success.
/// If hasTransparentOut is non-null, it is set to true when any transparent
/// (non-opaque) block face is included in the mesh.
/// If hasOpaqueOut is non-null, it is set to true when any opaque block face
/// is included in the mesh.
/// @see notes/mesher-capacity-accounting.md
bool greedyMesh(
    const uint8_t* voxels,
    const uint8_t* light,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut,
    bool* hasTransparentOut = nullptr,
    bool* hasOpaqueOut = nullptr);

} // namespace mesher
} // namespace voxel
