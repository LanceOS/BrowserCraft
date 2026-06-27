#pragma once

#include <cstdint>

namespace terrain {

class WorldGenPipeline;

namespace mesher {

struct MesherConfig {
  int32_t sizeX;
  int32_t sizeY;
  int32_t sizeZ;
  int32_t maxVertices;
  int32_t maxIndices;
  int32_t strideFloats;
};

/// Build a smooth terrain mesh for a chunk using the continuous terrain
/// density field.
bool smoothTerrainMesh(
    const WorldGenPipeline& pipeline,
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
} // namespace terrain
