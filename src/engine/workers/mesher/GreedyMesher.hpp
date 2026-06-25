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

/// Perform greedy meshing on a chunk's voxel data.
///
/// Reads block IDs from \a voxels and light data from \a light,
/// then writes interleaved vertex and index data into \a vertexOut
/// and \a indexOut.  \a vertexCountOut and \a indexCountOut receive
/// the number of floats written and the number of indices written.
///
/// Returns false if the vertex or index buffer capacity was exceeded
/// (the mesh is truncated).  Returns true on success.
/// If hasTransparentOut is non-null, it is set to true when any transparent
/// (non-opaque) block face is included in the mesh.
/// If hasOpaqueOut is non-null, it is set to true when any opaque block face
/// is included in the mesh.
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
