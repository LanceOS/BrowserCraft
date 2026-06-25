#include "GreedyMesher.hpp"
#include "AmbientOcclusion.hpp"
#include <cstring>
#include <algorithm>
#include <new>
#include <vector>

namespace voxel {
namespace mesher {

// ======================================================================
// Internal helpers
// ======================================================================

static inline auto voxelIndex(int32_t x, int32_t y, int32_t z,
                              const MesherConfig& cfg) -> int32_t {
  return (y * cfg.sizeZ + z) * cfg.sizeX + x;
}

static inline auto getBlock(const uint8_t* voxels,
                            int32_t x, int32_t y, int32_t z,
                            const MesherConfig& cfg) -> uint8_t {
  if (x < 0 || x >= cfg.sizeX) return 0;
  if (y < 0 || y >= cfg.sizeY) return 0;
  if (z < 0 || z >= cfg.sizeZ) return 0;
  return voxels[voxelIndex(x, y, z, cfg)];
}

/// True if block at (x,y,z) is present and marked opaque.
static inline auto isSolid(const uint8_t* voxels,
                           int32_t x, int32_t y, int32_t z,
                           const MesherConfig& cfg,
                           const BlockRegistry& blocks) -> bool {
  uint8_t id = getBlock(voxels, x, y, z, cfg);
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
                                    int32_t skipX, int32_t skipY, int32_t skipZ) -> bool {
  if (x == skipX && y == skipY && z == skipZ) return false;
  return isSolid(voxels, x, y, z, cfg, blocks);
}

static inline auto skyNibble(uint8_t packed) -> int32_t { return (packed >> 4) & 0x0F; }
static inline auto blockNibble(uint8_t packed) -> int32_t { return packed & 0x0F; }

static inline auto getPackedLight(const uint8_t* light,
                                  int32_t x, int32_t y, int32_t z,
                                  const MesherConfig& cfg) -> uint8_t {
  if (x < 0 || x >= cfg.sizeX) return 0;
  if (y < 0 || y >= cfg.sizeY) return 0;
  if (z < 0 || z >= cfg.sizeZ) return 0;
  return light[voxelIndex(x, y, z, cfg)];
}

/// Pack sky light, block light, AO into a float.
/// The shader does `uint(a_lightData + 0.5)` to recover the integer.
static inline auto packLight(int32_t sky, int32_t block, int32_t ao) -> float {
  uint32_t p = ( static_cast<uint32_t>(sky)   & 0x0Fu)
             | ((static_cast<uint32_t>(block) & 0x0Fu) << 4)
             | ((static_cast<uint32_t>(ao)    & 0x03u) << 16);
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
                             const BlockRegistry& blocks) {
  int32_t ly = faceY - 1; // blocks below the face
  bool s1 = isSolidExcluding(voxels, cx-1, ly, cz,   cfg, blocks, bx, by, bz);
  bool s2 = isSolidExcluding(voxels, cx,   ly, cz-1, cfg, blocks, bx, by, bz);
  bool c  = isSolidExcluding(voxels, cx-1, ly, cz-1, cfg, blocks, bx, by, bz);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoBottom(const uint8_t* voxels,
                                int32_t cx, int32_t faceY, int32_t cz,
                                int32_t bx, int32_t by, int32_t bz,
                                const MesherConfig& cfg,
                                const BlockRegistry& blocks) {
  int32_t ly = faceY + 1; // blocks above the face
  bool s1 = isSolidExcluding(voxels, cx-1, ly, cz,   cfg, blocks, bx, by, bz);
  bool s2 = isSolidExcluding(voxels, cx,   ly, cz-1, cfg, blocks, bx, by, bz);
  bool c  = isSolidExcluding(voxels, cx-1, ly, cz-1, cfg, blocks, bx, by, bz);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoRight(const uint8_t* voxels,
                               int32_t faceX, int32_t cy, int32_t cz,
                               int32_t bx, int32_t by, int32_t bz,
                               const MesherConfig& cfg,
                               const BlockRegistry& blocks) {
  int32_t lx = faceX - 1;
  bool s1 = isSolidExcluding(voxels, lx, cy-1, cz,   cfg, blocks, bx, by, bz);
  bool s2 = isSolidExcluding(voxels, lx, cy,   cz-1, cfg, blocks, bx, by, bz);
  bool c  = isSolidExcluding(voxels, lx, cy-1, cz-1, cfg, blocks, bx, by, bz);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoLeft(const uint8_t* voxels,
                              int32_t faceX, int32_t cy, int32_t cz,
                              int32_t bx, int32_t by, int32_t bz,
                              const MesherConfig& cfg,
                              const BlockRegistry& blocks) {
  int32_t lx = faceX + 1;
  bool s1 = isSolidExcluding(voxels, lx, cy-1, cz,   cfg, blocks, bx, by, bz);
  bool s2 = isSolidExcluding(voxels, lx, cy,   cz-1, cfg, blocks, bx, by, bz);
  bool c  = isSolidExcluding(voxels, lx, cy-1, cz-1, cfg, blocks, bx, by, bz);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoFront(const uint8_t* voxels,
                               int32_t cx, int32_t cy, int32_t faceZ,
                               int32_t bx, int32_t by, int32_t bz,
                               const MesherConfig& cfg,
                               const BlockRegistry& blocks) {
  int32_t lz = faceZ - 1;
  bool s1 = isSolidExcluding(voxels, cx-1, cy-1, lz, cfg, blocks, bx, by, bz);
  bool s2 = isSolidExcluding(voxels, cx,   cy-1, lz, cfg, blocks, bx, by, bz);
  bool c  = isSolidExcluding(voxels, cx-1, cy,   lz, cfg, blocks, bx, by, bz);
  return calculateAO(s1, s2, c);
}

static inline int32_t aoBack(const uint8_t* voxels,
                              int32_t cx, int32_t cy, int32_t faceZ,
                              int32_t bx, int32_t by, int32_t bz,
                              const MesherConfig& cfg,
                              const BlockRegistry& blocks) {
  int32_t lz = faceZ + 1;
  bool s1 = isSolidExcluding(voxels, cx-1, cy-1, lz, cfg, blocks, bx, by, bz);
  bool s2 = isSolidExcluding(voxels, cx,   cy-1, lz, cfg, blocks, bx, by, bz);
  bool c  = isSolidExcluding(voxels, cx-1, cy,   lz, cfg, blocks, bx, by, bz);
  return calculateAO(s1, s2, c);
}

// ======================================================================
// Corner light — average of four surrounding blocks
// ======================================================================

struct AvgLight { int32_t sky; int32_t block; };

static inline auto cornerLight(const uint8_t* light,
                               int32_t cx, int32_t cy, int32_t cz,
                               const MesherConfig& cfg) -> AvgLight {
  int32_t sTot = 0, bTot = 0, cnt = 0;
  int32_t ox[4] = {-1, 0, -1, 0};
  int32_t oz[4] = {-1, -1, 0, 0};
  for (int32_t i = 0; i < 4; ++i) {
    uint8_t p = getPackedLight(light, cx + ox[i], cy, cz + oz[i], cfg);
    sTot += skyNibble(p);
    bTot += blockNibble(p);
    ++cnt;
  }
  AvgLight r;
  r.sky   = (cnt > 0) ? ((sTot + cnt/2) / cnt) : 0;
  r.block = (cnt > 0) ? ((bTot + cnt/2) / cnt) : 0;
  return r;
}

// ======================================================================
// Vertex writer
// ======================================================================

static inline void writeVtx(float* data, int32_t stride, uint32_t offset,
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

namespace {
struct GreedyMeshScratch {
  std::vector<uint8_t> mask;
};
thread_local GreedyMeshScratch g_scratch;
}

// ======================================================================
// Public API
// ======================================================================

bool greedyMesh(
    const uint8_t* voxels,
    const uint8_t* light,
    const BlockRegistry& blocks,
    const MesherConfig& cfg,
    float* vertexOut,
    uint32_t* indexOut,
    uint32_t& vertexCountOut,
    uint32_t& indexCountOut,
    bool* hasTransparentOut)
{
  const int32_t SX = cfg.sizeX;
  const int32_t SY = cfg.sizeY;
  const int32_t SZ = cfg.sizeZ;
  const int32_t S  = cfg.strideFloats; // stride in floats (should be 10)
  const uint32_t maxV  = static_cast<uint32_t>(cfg.maxVertices);
  const uint32_t maxI  = static_cast<uint32_t>(cfg.maxIndices);

  uint32_t vo = 0; // float offset into vertexOut
  uint32_t io = 0; // index count

  // Scratch mask sized to the largest face for this chunk configuration.
  const auto neededScratch =
    static_cast<size_t>(std::max({SX * SY, SX * SZ, SY * SZ}));
  if (g_scratch.mask.size() < neededScratch) {
    g_scratch.mask.resize(neededScratch);
  }
  uint8_t* mask = g_scratch.mask.data();

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
    std::memset(mask, 0, needed);

    for (int32_t sl = 0; sl < sliceCnt; ++sl) {
      // ---- Build mask ----
      std::memset(mask, 0, needed);

      for (int32_t v = 0; v < vSz; ++v) {
        for (int32_t u = 0; u < uSz; ++u) {
          int32_t co[3];
          co[axis]  = sl;
          co[uAxis] = u;
          co[vAxis] = v;
          int32_t bx = co[0], by = co[1], bz = co[2];

          uint8_t bid = getBlock(voxels, bx, by, bz, cfg);
          if (bid == 0) continue;

          int32_t nn[3] = {bx + info.normal[0], by + info.normal[1], bz + info.normal[2]};
          uint8_t nid = getBlock(voxels, nn[0], nn[1], nn[2], cfg);
          if (nid != 0) {
            const auto* nd = blocks.tryGet(nid);
            if (nd && nd->material.opaque) continue;
          }
          mask[v * uSz + u] = bid;
        }
      }

      // ---- Greedy rectangle packing ----
      for (int32_t v = 0; v < vSz; ++v) {
        for (int32_t u = 0; u < uSz; ++u) {
          uint8_t bid = mask[v * uSz + u];
          if (bid == 0) continue;

          const auto* def = blocks.tryGet(bid);
          if (!def) { mask[v * uSz + u] = 0; continue; }
          if (hasTransparentOut && !def->material.opaque) {
            *hasTransparentOut = true;
          }

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
              if (mask[(v+vv)*uSz + u+rw] != bid) { ok = false; break; }
            if (!ok) break;
            ++rw;
          }
          while (v + rh < vSz) {
            bool ok = true;
            for (int32_t uu = 0; uu < rw; ++uu)
              if (mask[(v+rh)*uSz + u+uu] != bid) { ok = false; break; }
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

          // First block in the rect (for self-exclusion in AO)
          int32_t bx, by, bz;
          { int32_t co[3] = {sl,0,0}; co[uAxis]=u; co[vAxis]=v; bx=co[0]; by=co[1]; bz=co[2]; }

          // Light sample y-level
          int32_t lY;
          if (di == 2)      lY = static_cast<int32_t>(fc) - 1; // below top face
          else if (di == 3) lY = static_cast<int32_t>(fc);     // at bottom face
          else              lY = c[0][1];

          // AO & light per corner
          int32_t ao[4], sky[4], blk[4];
          for (int32_t ci = 0; ci < 4; ++ci) {
            int32_t cx = c[ci][0], cy = c[ci][1], cz = c[ci][2];
            switch (di) {
              case 0: ao[ci]=aoRight(voxels,(int32_t)fc,cy,cz,bx,by,bz,cfg,blocks); break;
              case 1: ao[ci]=aoLeft( voxels,(int32_t)fc,cy,cz,bx,by,bz,cfg,blocks); break;
              case 2: ao[ci]=aoTop(  voxels,cx,(int32_t)fc,cz,bx,by,bz,cfg,blocks); break;
              case 3: ao[ci]=aoBottom(voxels,cx,(int32_t)fc,cz,bx,by,bz,cfg,blocks); break;
              case 4: ao[ci]=aoFront(voxels,cx,cy,(int32_t)fc,bx,by,bz,cfg,blocks); break;
              case 5: ao[ci]=aoBack( voxels,cx,cy,(int32_t)fc,bx,by,bz,cfg,blocks); break;
            }
            AvgLight al = cornerLight(light, cx, lY, cz, cfg);
            sky[ci] = al.sky; blk[ci] = al.block;
          }

          float ld[4];
          for (int32_t ci = 0; ci < 4; ++ci)
            ld[ci] = packLight(sky[ci], blk[ci], ao[ci]);

          bool flip = (ao[0] + ao[2]) > (ao[1] + ao[3]);

          // Capacity
          if (vo + 4u*static_cast<uint32_t>(S) > maxV || io + 6 > maxI) {
            vertexCountOut = vo / S; indexCountOut = io; return false;
          }

          float tlF = static_cast<float>(tlayer);
          writeVtx(vertexOut, S, vo, bl[0],bl[1],bl[2], nx,ny,nz, uv[0][0],uv[0][1], tlF, ld[0]); vo += S;
          writeVtx(vertexOut, S, vo, br[0],br[1],br[2], nx,ny,nz, uv[1][0],uv[1][1], tlF, ld[1]); vo += S;
          writeVtx(vertexOut, S, vo, tl[0],tl[1],tl[2], nx,ny,nz, uv[2][0],uv[2][1], tlF, ld[2]); vo += S;
          writeVtx(vertexOut, S, vo, tr[0],tr[1],tr[2], nx,ny,nz, uv[3][0],uv[3][1], tlF, ld[3]); vo += S;

          uint32_t base = (vo / S) - 4;
          if (!flip) {
            indexOut[io++] = base;   indexOut[io++] = base+1; indexOut[io++] = base+2;
            indexOut[io++] = base+1; indexOut[io++] = base+3; indexOut[io++] = base+2;
          } else {
            indexOut[io++] = base;   indexOut[io++] = base+1; indexOut[io++] = base+3;
            indexOut[io++] = base;   indexOut[io++] = base+3; indexOut[io++] = base+2;
          }
        }
      }
    }
  }

  vertexCountOut = vo / S;
  indexCountOut  = io;
  return true;
}

} // namespace mesher
} // namespace voxel
