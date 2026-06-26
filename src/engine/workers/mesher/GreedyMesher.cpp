#include "GreedyMesher.hpp"
#include "AmbientOcclusion.hpp"
#include "LightSampling.hpp"
#include "world/BlockIds.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {
namespace mesher {

namespace {

constexpr uint32_t kPackedVertexFloats = 10u;

} // namespace

// ======================================================================
// Vertex writer
// ======================================================================

static inline void writeVtx(float* data, uint32_t offset,
                            float x, float y, float z,
                            float nx, float ny, float nz,
                            float u, float v,
                            float texLayer, float lightData) {
  float* p = data + offset;
  p[0]=x; p[1]=y; p[2]=z;       // a_pos
  p[3]=nx; p[4]=ny; p[5]=nz;    // a_normal
  p[6]=u; p[7]=v;               // a_uv
  p[8]=texLayer;                 // a_texLayer
  p[9]=lightData;                // a_lightData
}

namespace {
struct GreedyMeshScratch {
  std::vector<uint16_t> mask;
  std::vector<uint32_t> transparentIndices;
};
thread_local GreedyMeshScratch g_scratch;

auto isOverlayRenderableBlock(const BlockDefinition& def) -> bool {
  switch (def.id) {
    case BlockId::GRASS:
    case BlockId::DIRT:
    case BlockId::STONE:
    case BlockId::SAND:
    case BlockId::BEDROCK:
    case BlockId::MOSSY_STONE:
      return false;
    default:
      return true;
  }
}
}

// ======================================================================
// Public API
// ======================================================================

auto estimateMeshCapacity(
    const uint8_t* voxels,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    const NeighborVoxelViews& neighbors) -> MeshCapacityHint
{
  MeshCapacityHint hint{};
  if (!voxels) return hint;
  if (cfg.sizeX <= 0 || cfg.sizeY <= 0 || cfg.sizeZ <= 0) return hint;

  static constexpr int32_t kNeighborOffsets[6][3] = {
    { 1, 0, 0}, {-1, 0, 0},
    { 0, 1, 0}, { 0,-1, 0},
    { 0, 0, 1}, { 0, 0,-1},
  };

  for (int32_t y = 0; y < cfg.sizeY; ++y) {
    for (int32_t z = 0; z < cfg.sizeZ; ++z) {
      for (int32_t x = 0; x < cfg.sizeX; ++x) {
        uint8_t bid = getBlock(voxels, x, y, z, cfg, neighbors);
        if (bid == 0) continue;

        const auto* def = blocks.tryGet(bid);
        if (!def) continue;

        for (const auto& off : kNeighborOffsets) {
          const int32_t nx = x + off[0];
          const int32_t ny = y + off[1];
          const int32_t nz = z + off[2];
          if (isOpaqueBlock(getBlock(voxels, nx, ny, nz, cfg, neighbors), blocks)) {
            continue;
          }
          ++hint.quadCount;
        }
      }
    }
  }

  hint.vertexCount = hint.quadCount * 4u;
  hint.indexCount = hint.quadCount * 6u;
  return hint;
}

// @see notes/mesher-capacity-accounting.md
template <typename ShouldEmitFn>
bool meshVolumeImpl(
    const uint8_t* voxels,
    const uint8_t* light,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    uint32_t vertexBase,
    uint32_t opaqueIndexBase,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut,
    bool* hasTransparentOut,
    bool* hasOpaqueOut,
    uint32_t* opaqueIndexCountOut,
    uint32_t* transparentIndexCountOut,
    const NeighborVoxelViews& neighbors,
    ShouldEmitFn&& shouldEmitBlock)
{
  vertexCountOut = vertexBase;
  indexCountOut = opaqueIndexBase;
  if (hasTransparentOut) *hasTransparentOut = false;
  if (hasOpaqueOut) *hasOpaqueOut = opaqueIndexBase > 0u;
  if (opaqueIndexCountOut) *opaqueIndexCountOut = opaqueIndexBase;
  if (transparentIndexCountOut) *transparentIndexCountOut = 0;

  if (!voxels || !light || !vertexOut || !indexOut) return false;
  if (cfg.sizeX <= 0 || cfg.sizeY <= 0 || cfg.sizeZ <= 0) return false;
  if (cfg.maxVertices < 0 || cfg.maxIndices < 0) return false;
  if (cfg.strideFloats < static_cast<int32_t>(kPackedVertexFloats)) return false;

  const int32_t SX = cfg.sizeX;
  const int32_t SY = cfg.sizeY;
  const int32_t SZ = cfg.sizeZ;
  const int32_t S  = cfg.strideFloats; // stride in floats (should be 10)
  const uint32_t maxVertices = static_cast<uint32_t>(cfg.maxVertices);
  const uint32_t maxI  = static_cast<uint32_t>(cfg.maxIndices);
  const uint32_t strideFloats = static_cast<uint32_t>(S);

  if (vertexBase > maxVertices || opaqueIndexBase > maxI) return false;

  uint32_t vo = vertexBase * strideFloats; // float offset into vertexOut
  uint32_t opaqueIo = opaqueIndexBase;      // opaque index count

  // Scratch mask sized to the largest face for this chunk configuration.
  const auto neededScratch =
    static_cast<size_t>(std::max({SX * SY, SX * SZ, SY * SZ}));
  if (g_scratch.mask.size() < neededScratch) {
    g_scratch.mask.resize(neededScratch);
  }
  g_scratch.transparentIndices.clear();
  uint16_t* mask = g_scratch.mask.data();

  for (int32_t di = 0; di < 6; ++di) {
    const auto& info = kDirs[di];
    int32_t axis  = info.axis;
    int32_t uAxis = info.uAxis;
    int32_t vAxis = info.vAxis;
    int32_t sign  = info.sign;

    float nx = static_cast<float>(info.normal[0]);
    float ny = static_cast<float>(info.normal[1]);
    float nz = static_cast<float>(info.normal[2]);

    int32_t sizes[3] = {SX, SY, SZ};
    int32_t sliceCnt = sizes[axis];
    int32_t uSz      = sizes[uAxis];
    int32_t vSz      = sizes[vAxis];

    size_t needed = static_cast<size_t>(uSz) * vSz;

    for (int32_t sl = 0; sl < sliceCnt; ++sl) {
      // ---- Build mask ----
      std::memset(mask, 0, needed * sizeof(uint16_t));

      for (int32_t v = 0; v < vSz; ++v) {
        for (int32_t u = 0; u < uSz; ++u) {
          int32_t co[3];
          co[axis]  = sl;
          co[uAxis] = u;
          co[vAxis] = v;
          int32_t bx = co[0], by = co[1], bz = co[2];

          uint8_t bid = getBlock(voxels, bx, by, bz, cfg, neighbors);
          if (bid == 0) continue;

          int32_t nn[3] = {bx + info.normal[0], by + info.normal[1], bz + info.normal[2]};
          uint8_t nid = getBlock(voxels, nn[0], nn[1], nn[2], cfg, neighbors);
          if (nid != 0) {
            const auto* nd = blocks.tryGet(nid);
            if (nd && nd->material.opaque) continue;
          }
          uint8_t aoKey = computeFaceAOPacked(voxels, di, sl, u, v, info, cfg, blocks, neighbors);
          mask[v * uSz + u] = static_cast<uint16_t>(bid) | (static_cast<uint16_t>(aoKey) << 8);
        }
      }

      // ---- Greedy rectangle packing ----
      for (int32_t v = 0; v < vSz; ++v) {
        for (int32_t u = 0; u < uSz; ++u) {
          uint16_t maskVal = mask[v * uSz + u];
          if (maskVal == 0) continue;
          uint8_t bid = static_cast<uint8_t>(maskVal & 0xFF);

          const auto* def = blocks.tryGet(bid);
          if (!def || !shouldEmitBlock(*def)) { mask[v * uSz + u] = 0; continue; }
          if (hasTransparentOut && !def->material.opaque) *hasTransparentOut = true;
          if (hasOpaqueOut && def->material.opaque) *hasOpaqueOut = true;

          // Texture layer for this face direction
          uint16_t tlayer;
          switch (di) {
            case 0: case 1: tlayer = def->textures.side;   break;
            case 2:         tlayer = def->textures.top;    break;
            case 3:         tlayer = def->textures.bottom; break;
            case 4: case 5: tlayer = def->textures.side;   break;
            default:        tlayer = 0; break;
          }

          // Grow rectangle
          int32_t rw = 1, rh = 1;

          while (u + rw < uSz) {
            bool ok = true;
            for (int32_t vv = 0; vv < rh; ++vv)
              if (mask[(v+vv)*uSz + u+rw] != maskVal) { ok = false; break; }
            if (!ok) break;
            ++rw;
          }
          while (v + rh < vSz) {
            bool ok = true;
            for (int32_t uu = 0; uu < rw; ++uu)
              if (mask[(v+rh)*uSz + u+uu] != maskVal) { ok = false; break; }
            if (!ok) break;
            ++rh;
          }

          // Clear mask
          for (int32_t vv = 0; vv < rh; ++vv)
            for (int32_t uu = 0; uu < rw; ++uu)
              mask[(v+vv)*uSz + u+uu] = 0;

          // ----- Emit quad -----
          float fc = static_cast<float>(sl) + (sign > 0 ? 1.0f : 0.0f);

          // 4 corners
          float bl[3]={}, br[3]={}, tl[3]={}, tr[3]={};
          bl[axis] = br[axis] = tl[axis] = tr[axis] = fc;
          bl[uAxis] = static_cast<float>(u);       br[uAxis] = static_cast<float>(u+rw);
          tl[uAxis] = static_cast<float>(u);       tr[uAxis] = static_cast<float>(u+rw);
          bl[vAxis] = static_cast<float>(v);       br[vAxis] = static_cast<float>(v);
          tl[vAxis] = static_cast<float>(v+rh);    tr[vAxis] = static_cast<float>(v+rh);

          float uv[4][2] = {
            {0,0}, {static_cast<float>(rw),0},
            {0,static_cast<float>(rh)}, {static_cast<float>(rw),static_cast<float>(rh)},
          };

          // Grid corner integers
          int32_t c[4][3] = {
            {static_cast<int32_t>(bl[0]), static_cast<int32_t>(bl[1]), static_cast<int32_t>(bl[2])},
            {static_cast<int32_t>(br[0]), static_cast<int32_t>(br[1]), static_cast<int32_t>(br[2])},
            {static_cast<int32_t>(tl[0]), static_cast<int32_t>(tl[1]), static_cast<int32_t>(tl[2])},
            {static_cast<int32_t>(tr[0]), static_cast<int32_t>(tr[1]), static_cast<int32_t>(tr[2])},
          };

          // Extract pre-computed AO from the mask key — all faces in this
          // merged rect are guaranteed to share the same AO pattern.
          uint8_t aoKey = static_cast<uint8_t>((maskVal >> 8) & 0xFF);
          int32_t ao[4], sky[4], block[4];
          for (int32_t ci = 0; ci < 4; ++ci) {
            ao[ci] = (aoKey >> (ci * 2)) & 0x3;
            AvgLight al = cornerLight(light, axis, sign, uAxis, vAxis, c[ci], cfg);
            sky[ci] = al.sky;
            block[ci] = al.block;
          }

          float ld[4];
          for (int32_t ci = 0; ci < 4; ++ci)
            ld[ci] = packLight(sky[ci], block[ci], ao[ci]);

          bool flip = (ao[0] + ao[2]) > (ao[1] + ao[3]);

          // Capacity
          const uint32_t nextVertexCount = (vo / strideFloats) + 4u;
          const bool faceOpaque = def->material.opaque;
          const uint32_t transparentIo = static_cast<uint32_t>(g_scratch.transparentIndices.size());
          const uint32_t nextIndexCount = opaqueIo + transparentIo + 6u;
          if (nextVertexCount > maxVertices || nextIndexCount > maxI) {
            vertexCountOut = vo / strideFloats;
            indexCountOut = opaqueIo + transparentIo;
            if (opaqueIndexCountOut) *opaqueIndexCountOut = opaqueIo;
            if (transparentIndexCountOut) *transparentIndexCountOut = transparentIo;
            return false;
          }

          float tlF = static_cast<float>(tlayer);
          writeVtx(vertexOut, vo, bl[0],bl[1],bl[2], nx,ny,nz, uv[0][0],uv[0][1], tlF, ld[0]); vo += S;
          writeVtx(vertexOut, vo, br[0],br[1],br[2], nx,ny,nz, uv[1][0],uv[1][1], tlF, ld[1]); vo += S;
          writeVtx(vertexOut, vo, tl[0],tl[1],tl[2], nx,ny,nz, uv[2][0],uv[2][1], tlF, ld[2]); vo += S;
          writeVtx(vertexOut, vo, tr[0],tr[1],tr[2], nx,ny,nz, uv[3][0],uv[3][1], tlF, ld[3]); vo += S;

          uint32_t base = (vo / strideFloats) - 4;
          auto emitIndex = [&](uint32_t idx) {
            if (faceOpaque) {
              indexOut[opaqueIo++] = idx;
            } else {
              g_scratch.transparentIndices.push_back(idx);
            }
          };

          // Faces X+ (di=0), Y+ (di=2), and Z- (di=5) produce a computed
          // normal opposite to the face normal with the default winding.
          // Reverse the winding for those three directions so that the
          // geometric normal matches the per-vertex normal, giving correct
          // diffuse lighting.
          bool reverseWind = (di == 0 || di == 2 || di == 5);

          if (!flip) {
            if (!reverseWind) {
              emitIndex(base);   emitIndex(base+1); emitIndex(base+2);
              emitIndex(base+1); emitIndex(base+3); emitIndex(base+2);
            } else {
              emitIndex(base);   emitIndex(base+2); emitIndex(base+3);
              emitIndex(base+1); emitIndex(base);   emitIndex(base+3);
            }
          } else {
            if (!reverseWind) {
              emitIndex(base);   emitIndex(base+1); emitIndex(base+3);
              emitIndex(base);   emitIndex(base+3); emitIndex(base+2);
            } else {
              emitIndex(base);   emitIndex(base+2); emitIndex(base+1);
              emitIndex(base+2); emitIndex(base+3); emitIndex(base+1);
            }
          }
        }
      }
    }
  }

  const uint32_t transparentIo = static_cast<uint32_t>(g_scratch.transparentIndices.size());
  if (transparentIo > 0u) {
    std::memcpy(indexOut + opaqueIo, g_scratch.transparentIndices.data(),
                static_cast<size_t>(transparentIo) * sizeof(uint32_t));
  }
  vertexCountOut = vo / strideFloats;
  indexCountOut  = opaqueIo + transparentIo;
  if (opaqueIndexCountOut) *opaqueIndexCountOut = opaqueIo;
  if (transparentIndexCountOut) *transparentIndexCountOut = transparentIo;
  return true;
}

bool greedyMesh(
    const uint8_t* voxels,
    const uint8_t* light,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut,
    bool* hasTransparentOut,
    bool* hasOpaqueOut,
    uint32_t* opaqueIndexCountOut,
    uint32_t* transparentIndexCountOut,
    const NeighborVoxelViews& neighbors)
{
  return meshVolumeImpl(
      voxels, light, blocks, cfg,
      0u, 0u,
      vertexOut, indexOut,
      vertexCountOut, indexCountOut,
      hasTransparentOut, hasOpaqueOut,
      opaqueIndexCountOut, transparentIndexCountOut,
      neighbors,
      [](const BlockDefinition&) { return true; });
}

bool overlayGreedyMesh(
    const uint8_t* voxels,
    const uint8_t* light,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    uint32_t vertexBase,
    uint32_t opaqueIndexBase,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut,
    bool* hasTransparentOut,
    bool* hasOpaqueOut,
    uint32_t* opaqueIndexCountOut,
    uint32_t* transparentIndexCountOut,
    const NeighborVoxelViews& neighbors)
{
  return meshVolumeImpl(
      voxels, light, blocks, cfg,
      vertexBase, opaqueIndexBase,
      vertexOut, indexOut,
      vertexCountOut, indexCountOut,
      hasTransparentOut, hasOpaqueOut,
      opaqueIndexCountOut, transparentIndexCountOut,
      neighbors,
      [](const BlockDefinition& def) { return isOverlayRenderableBlock(def); });
}

} // namespace mesher
} // namespace voxel
