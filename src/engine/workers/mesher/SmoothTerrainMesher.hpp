#pragma once

#include "GreedyMesher.hpp"
#include <cstdint>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

class BlockRegistry;
class WorldGenPipeline;

namespace mesher {

/// Build a smooth terrain mesh for a chunk using the continuous terrain
/// density field. The output uses the same vertex layout as the greedy voxel
/// mesher so the renderer can choose between block and terrain shaders without
/// changing the buffer layout.
///
/// The mesh is flat-shaded for now. Each triangle gets a single normal and a
/// terrain material sample derived from slope, depth, and biome context.
bool smoothTerrainMesh(
    const WorldGenPipeline& pipeline,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    int32_t chunkX,
    int32_t chunkZ,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut,
    uint32_t* opaqueIndexCountOut = nullptr,
    uint32_t* transparentIndexCountOut = nullptr);

} // namespace mesher
} // namespace voxel
