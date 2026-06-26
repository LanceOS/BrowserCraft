#include "GreedyMesher.hpp"
#include "AmbientOcclusion.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace voxel {
namespace mesher {

namespace {

constexpr uint32_t kPackedVertexFloats = 10u;

} // namespace

// ======================================================================
// Internal helpers
// ======================================================================

static inline auto voxelIndex(int32_t x, int32_t y, int32_t z,
                              const MesherConfig& cfg) -> int32_t {
  return (y * cfg.sizeZ + z) * cfg.sizeX + x;
}

static inline auto getBlock(const uint8_t* voxels,
                            int32_t x, int32_t y, int32_t z,
                            const MesherConfig& cfg,
                            const NeighborVoxelViews& neighbors) -> uint8_t {
  if (y < 0 || y >= cfg.sizeY) return 0;
  if (x >= 0 && x < cfg.sizeX && z >= 0 && z < cfg.sizeZ) {
    return voxels[voxelIndex(x, y, z, cfg)];
  }

  if (z >= 0 && z < cfg.sizeZ) {
    if (x < 0 && neighbors.nx) {
      return neighbors.nx[voxelIndex(cfg.sizeX - 1, y, z, cfg)];
    }
    if (x >= cfg.sizeX && neighbors.px) {
      return neighbors.px[voxelIndex(0, y, z, cfg)];
    }
  }

  if (x >= 0 && x < cfg.sizeX) {
    if (z < 0 && neighbors.nz) {
      return neighbors.nz[voxelIndex(x, y, cfg.sizeZ - 1, cfg)];
    }
    if (z >= cfg.sizeZ && neighbors.pz) {
      return neighbors.pz[voxelIndex(x, y, 0, cfg)];
    }
  }

  return 0;
}

static inline auto getBlock(const uint8_t* voxels,
                            int32_t x, int32_t y, int32_t z,
                            const MesherConfig& cfg) -> uint8_t {
  static constexpr NeighborVoxelViews kNoNeighbors{};
  return getBlock(voxels, x, y, z, cfg, kNoNeighbors);
}

static inline auto isOpaqueBlock(uint8_t blockId, const BlockRegistry& blocks) -> bool {
  if (blockId == 0) return false;
  const auto* def = blocks.tryGet(blockId);
  return def && def->material.opaque;
}

/// True if block at (x,y,z) is present and marked opaque.
static inline auto isSolid(const uint8_t* voxels,
                           int32_t x, int32_t y, int32_t z,
                           const MesherConfig& cfg,
                           const BlockRegistry& blocks,
                           const NeighborVoxelViews& neighbors) -> bool {
  uint8_t id = getBlock(voxels, x, y, z, cfg, neighbors);
  if (id == 0) return false;
  const auto* def = blocks.tryGet(id);
  return def && def->material.opaque;
}

/// Like isSolid but never counts the block at (skipX,skipY,skipZ)
/// (used to avoid self-occlusion in AO).
static inline auto isSolidExcluding(const uint8_t* voxels,
                                    int32_t x, int32_t y, int32_t z,
                                    const MesherConfig& cfg,
                                    const BlockRegistry& blocks,
                                    int32_t skipX, int32_t skipY, int32_t skipZ,
                                    const NeighborVoxelViews& neighbors) -> bool {
  if (x == skipX && y == skipY && z == skipZ) return false;
  return isSolid(voxels, x, y, z, cfg, blocks, neighbors);
}

static inline auto skyNibble(uint8_t packed) -> int32_t { return (packed >> 4) & 0x0F; }
static inline auto blockNibble(uint8_t packed) -> int32_t { return packed & 0x0F; }

// @see notes/chunk-border-light-seams.md
static inline auto getPackedLight(const uint8_t* light,
                                  int32_t x, int32_t y, int32_t z,
                                  const MesherConfig& cfg) -> uint8_t {
  // Clamp instead of returning 0 so border vertices do not blend in
  // artificial darkness when a chunk has no neighbor data available.
  x = std::clamp(x, 0, cfg.sizeX - 1);
  y = std::clamp(y, 0, cfg.sizeY - 1);
  z = std::clamp(z, 0, cfg.sizeZ - 1);
  return light[voxelIndex(x, y, z, cfg)];
}

// @see notes/chunk-shadow-banding.md
/// Pack sky light, block light, and AO into a float.
/// The shader does `uint(a_lightData + 0.5)` to recover the integer.
static inline auto packLight(int32_t sky, int32_t block, int32_t ao) -> float {
  uint32_t p = ( static_cast<uint32_t>(sky) & 0x0Fu)
             | ((static_cast<uint32_t>(block) & 0x0Fu) << 4)
             | ((static_cast<uint32_t>(ao)  & 0x03u) << 16);
  return static_cast<float>(p);
}

// ======================================================================
// Ambient occlusion per face direction
// ======================================================================

// For a face with normal N and a corner at grid position C,
// the three "inside" blocks that can occlude the corner are:
//   C - N + T1     (one step opposite the normal, one step along tangent T1)
//   C - N + T2     (one step opposite the normal, one step along tangent T2)
//   C - N + T1+T2  (diagonal)
// where T1,T2 are the two axes perpendicular to N, with signs chosen so
// the checked positions are NOT the block being rendered and NOT the
// neighbour already culled.  We use isSolidExcluding to skip self.

static inline int32_t aoTop(const uint8_t* voxels,
                             int32_t cx, int32_t faceY, int32_t cz,
                             int32_t bx, int32_t by, int32_t bz,
                             const MesherConfig& cfg,
                             const BlockRegistry& blocks,
                             const NeighborVoxelViews& neighbors) {
  int32_t ly = faceY; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, ly, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoBottom(const uint8_t* voxels,
                                int32_t cx, int32_t faceY, int32_t cz,
                                int32_t bx, int32_t by, int32_t bz,
                                const MesherConfig& cfg,
                                const BlockRegistry& blocks,
                                const NeighborVoxelViews& neighbors) {
  int32_t ly = faceY - 1; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, ly, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoRight(const uint8_t* voxels,
                               int32_t faceX, int32_t cy, int32_t cz,
                               int32_t bx, int32_t by, int32_t bz,
                               const MesherConfig& cfg,
                               const BlockRegistry& blocks,
                               const NeighborVoxelViews& neighbors) {
  int32_t lx = faceX; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, lx, cy-1, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, lx, cy,   cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, lx, cy-1, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoLeft(const uint8_t* voxels,
                              int32_t faceX, int32_t cy, int32_t cz,
                              int32_t bx, int32_t by, int32_t bz,
                              const MesherConfig& cfg,
                              const BlockRegistry& blocks,
                              const NeighborVoxelViews& neighbors) {
  int32_t lx = faceX - 1; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, lx, cy-1, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, lx, cy,   cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, lx, cy-1, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoFront(const uint8_t* voxels,
                               int32_t cx, int32_t cy, int32_t faceZ,
                               int32_t bx, int32_t by, int32_t bz,
                               const MesherConfig& cfg,
                               const BlockRegistry& blocks,
                               const NeighborVoxelViews& neighbors) {
  int32_t lz = faceZ; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, cy,   lz, cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoBack(const uint8_t* voxels,
                              int32_t cx, int32_t cy, int32_t faceZ,
                              int32_t bx, int32_t by, int32_t bz,
                              const MesherConfig& cfg,
                              const BlockRegistry& blocks,
                              const NeighborVoxelViews& neighbors) {
  int32_t lz = faceZ - 1; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, cy,   lz, cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

// ======================================================================
// Corner light — average of four surrounding blocks
// ======================================================================

struct AvgLight {
  int32_t sky = 0;
  int32_t block = 0;
};

static inline auto cornerLight(const uint8_t* light,
                               int32_t axis, int32_t sign,
                               int32_t uAxis, int32_t vAxis,
                               const int32_t corner[3],
                               const MesherConfig& cfg) -> AvgLight {
  const int32_t airAxisCoord = corner[axis] + (sign < 0 ? -1 : 0);

  int32_t skyTotal = 0;
  int32_t blockTotal = 0;
  int32_t count = 0;

  for (int32_t du = -1; du <= 0; ++du) {
    for (int32_t dv = -1; dv <= 0; ++dv) {
      int32_t sample[3] = {corner[0], corner[1], corner[2]};
      sample[axis] = airAxisCoord;
      sample[uAxis] += du;
      sample[vAxis] += dv;

      const uint8_t packed = getPackedLight(light, sample[0], sample[1], sample[2], cfg);
      skyTotal += skyNibble(packed);
      blockTotal += blockNibble(packed);
      ++count;
    }
  }

  AvgLight result;
  if (count > 0) {
    result.sky = (skyTotal + count / 2) / count;
    result.block = (blockTotal + count / 2) / count;
  }
  return result;
}

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

// ======================================================================
// Direction table
// ======================================================================

struct DirInfo {
  int8_t normal[3];
  int8_t axis;   // constant axis
  int8_t uAxis;  // right in mask
  int8_t vAxis;  // down in mask
  int8_t sign;   // +1 = pos, -1 = neg
};

static constexpr DirInfo kDirs[6] = {
  {{ 1, 0, 0}, 0, 2, 1,  1},  // X+ (right)  — uAxis=Z, vAxis=Y so texture V maps to world Y
  {{-1, 0, 0}, 0, 2, 1, -1},  // X- (left)   — uAxis=Z, vAxis=Y so texture V maps to world Y
  {{ 0, 1, 0}, 1, 0, 2,  1},  // Y+ (top)
  {{ 0,-1, 0}, 1, 0, 2, -1},  // Y- (bottom)
  {{ 0, 0, 1}, 2, 0, 1,  1},  // Z+ (front)  — uAxis=X, vAxis=Y ✓
  {{ 0, 0,-1}, 2, 0, 1, -1},  // Z- (back)   — uAxis=X, vAxis=Y ✓
};

// Pre-compute the packed AO key for a single 1x1 face.
// Returns 8 bits: 4 corners × 2 bits each (AO values 0-3).
static inline uint8_t computeFaceAOPacked(
    const uint8_t* voxels, int32_t di, int32_t sl, int32_t u, int32_t v,
    const DirInfo& info, const MesherConfig& cfg, const BlockRegistry& blocks,
    const NeighborVoxelViews& neighbors)
{
  const int32_t axis  = info.axis;
  const int32_t uAxis = info.uAxis;
  const int32_t vAxis = info.vAxis;
  const int32_t sign  = info.sign;
  const int32_t fc = sl + (sign > 0 ? 1 : 0);

  // Block position (self-exclusion target)
  int32_t co[3]; co[axis] = sl; co[uAxis] = u; co[vAxis] = v;
  const int32_t bx = co[0], by = co[1], bz = co[2];

  // 4 corner positions: bl, br, tl, tr
  int32_t corners[4][3];
  corners[0][axis] = fc; corners[0][uAxis] = u;     corners[0][vAxis] = v;
  corners[1][axis] = fc; corners[1][uAxis] = u + 1; corners[1][vAxis] = v;
  corners[2][axis] = fc; corners[2][uAxis] = u;     corners[2][vAxis] = v + 1;
  corners[3][axis] = fc; corners[3][uAxis] = u + 1; corners[3][vAxis] = v + 1;

  uint8_t packed = 0;
  for (int32_t ci = 0; ci < 4; ++ci) {
    const int32_t cx = corners[ci][0], cy = corners[ci][1], cz = corners[ci][2];
    int32_t ao;
    switch (di) {
      case 0: ao = aoRight (voxels, fc, cy, cz, bx, by, bz, cfg, blocks, neighbors); break;
      case 1: ao = aoLeft  (voxels, fc, cy, cz, bx, by, bz, cfg, blocks, neighbors); break;
      case 2: ao = aoTop   (voxels, cx, fc, cz, bx, by, bz, cfg, blocks, neighbors); break;
      case 3: ao = aoBottom(voxels, cx, fc, cz, bx, by, bz, cfg, blocks, neighbors); break;
      case 4: ao = aoFront (voxels, cx, cy, fc, bx, by, bz, cfg, blocks, neighbors); break;
      case 5: ao = aoBack  (voxels, cx, cy, fc, bx, by, bz, cfg, blocks, neighbors); break;
      default: ao = 3; break;
    }
    packed |= static_cast<uint8_t>((ao & 0x3) << (ci * 2));
  }
  return packed;
}

namespace {
struct GreedyMeshScratch {
  std::vector<uint16_t> mask;
  std::vector<uint32_t> transparentIndices;
};
thread_local GreedyMeshScratch g_scratch;
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
  vertexCountOut = 0;
  indexCountOut = 0;
  if (hasTransparentOut) *hasTransparentOut = false;
  if (hasOpaqueOut) *hasOpaqueOut = false;
  if (opaqueIndexCountOut) *opaqueIndexCountOut = 0;
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

  uint32_t vo = 0; // float offset into vertexOut
  uint32_t opaqueIo = 0; // opaque index count

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
          if (!def) { mask[v * uSz + u] = 0; continue; }
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

} // namespace mesher
} // namespace voxel
