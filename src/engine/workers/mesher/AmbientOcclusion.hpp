#pragma once

#include "LightSampling.hpp"
#include <array>
#include <cstdint>

namespace voxel {
namespace mesher {

/**
 * Precomputed AO Lookup Table.
 *
 * We pack the three boolean states (side1, side2, corner) into a 3-bit integer (0-7)
 * and use a precomputed uint8_t LUT. This eliminates branching in the hot meshing loop.
 *
 * Complexity: O(1) with zero branches.
 */
constexpr std::array<uint8_t, 8> generateAOLUT() {
  std::array<uint8_t, 8> lut{};
  for (int i = 0; i < 8; i++) {
    int s1 = (i >> 2) & 1;
    int s2 = (i >> 1) & 1;
    int c  = (i >> 0) & 1;
    if (s1 && s2) {
      lut[i] = 0;
    } else {
      lut[i] = 3 - (s1 + s2 + c);
    }
  }
  return lut;
}

inline constexpr auto AO_LUT = generateAOLUT();

inline auto calculateAO(bool s1, bool s2, bool c) -> uint8_t {
  // Pack into 3 bits: [s1, s2, c] -> index 0..7
  int index = ((s1 & 1) << 2) | ((s2 & 1) << 1) | (c & 1);
  return AO_LUT[index];
}

inline auto getBlock(const uint8_t* voxels,
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

inline auto getBlock(const uint8_t* voxels,
                     int32_t x, int32_t y, int32_t z,
                     const MesherConfig& cfg) -> uint8_t {
  static constexpr NeighborVoxelViews kNoNeighbors{};
  return getBlock(voxels, x, y, z, cfg, kNoNeighbors);
}

inline auto isOpaqueBlock(uint8_t blockId, const BlockRegistry& blocks) -> bool {
  if (blockId == 0) return false;
  const auto* def = blocks.tryGet(blockId);
  return def && def->material.opaque;
}

/// True if block at (x,y,z) is present and marked opaque.
inline auto isSolid(const uint8_t* voxels,
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
inline auto isSolidExcluding(const uint8_t* voxels,
                             int32_t x, int32_t y, int32_t z,
                             const MesherConfig& cfg,
                             const BlockRegistry& blocks,
                             int32_t skipX, int32_t skipY, int32_t skipZ,
                             const NeighborVoxelViews& neighbors) -> bool {
  if (x == skipX && y == skipY && z == skipZ) return false;
  return isSolid(voxels, x, y, z, cfg, blocks, neighbors);
}

struct DirInfo {
  int8_t normal[3];
  int8_t axis;   // constant axis
  int8_t uAxis;  // right in mask
  int8_t vAxis;  // down in mask
  int8_t sign;   // +1 = pos, -1 = neg
};

inline constexpr DirInfo kDirs[6] = {
  {{ 1, 0, 0}, 0, 2, 1,  1},  // X+ (right)  — uAxis=Z, vAxis=Y so texture V maps to world Y
  {{-1, 0, 0}, 0, 2, 1, -1},  // X- (left)   — uAxis=Z, vAxis=Y so texture V maps to world Y
  {{ 0, 1, 0}, 1, 0, 2,  1},  // Y+ (top)
  {{ 0,-1, 0}, 1, 0, 2, -1},  // Y- (bottom)
  {{ 0, 0, 1}, 2, 0, 1,  1},  // Z+ (front)  — uAxis=X, vAxis=Y ✓
  {{ 0, 0,-1}, 2, 0, 1, -1},  // Z- (back)   — uAxis=X, vAxis=Y ✓
};

// For a face with normal N and a corner at grid position C,
// the three "inside" blocks that can occlude the corner are:
//   C - N + T1     (one step opposite the normal, one step along tangent T1)
//   C - N + T2     (one step opposite the normal, one step along tangent T2)
//   C - N + T1+T2  (diagonal)
// where T1,T2 are the two axes perpendicular to N, with signs chosen so
// the checked positions are NOT the block being rendered and NOT the
// neighbour already culled.  We use isSolidExcluding to skip self.
inline auto aoTop(const uint8_t* voxels,
                  int32_t cx, int32_t faceY, int32_t cz,
                  int32_t bx, int32_t by, int32_t bz,
                  const MesherConfig& cfg,
                  const BlockRegistry& blocks,
                  const NeighborVoxelViews& neighbors) -> int32_t {
  int32_t ly = faceY; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, ly, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

inline auto aoBottom(const uint8_t* voxels,
                     int32_t cx, int32_t faceY, int32_t cz,
                     int32_t bx, int32_t by, int32_t bz,
                     const MesherConfig& cfg,
                     const BlockRegistry& blocks,
                     const NeighborVoxelViews& neighbors) -> int32_t {
  int32_t ly = faceY - 1; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, ly, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, ly, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

inline auto aoRight(const uint8_t* voxels,
                    int32_t faceX, int32_t cy, int32_t cz,
                    int32_t bx, int32_t by, int32_t bz,
                    const MesherConfig& cfg,
                    const BlockRegistry& blocks,
                    const NeighborVoxelViews& neighbors) -> int32_t {
  int32_t lx = faceX; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, lx, cy-1, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, lx, cy,   cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, lx, cy-1, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

inline auto aoLeft(const uint8_t* voxels,
                   int32_t faceX, int32_t cy, int32_t cz,
                   int32_t bx, int32_t by, int32_t bz,
                   const MesherConfig& cfg,
                   const BlockRegistry& blocks,
                   const NeighborVoxelViews& neighbors) -> int32_t {
  int32_t lx = faceX - 1; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, lx, cy-1, cz,   cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, lx, cy,   cz-1, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, lx, cy-1, cz-1, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

inline auto aoFront(const uint8_t* voxels,
                    int32_t cx, int32_t cy, int32_t faceZ,
                    int32_t bx, int32_t by, int32_t bz,
                    const MesherConfig& cfg,
                    const BlockRegistry& blocks,
                    const NeighborVoxelViews& neighbors) -> int32_t {
  int32_t lz = faceZ; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, cy,   lz, cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

inline auto aoBack(const uint8_t* voxels,
                   int32_t cx, int32_t cy, int32_t faceZ,
                   int32_t bx, int32_t by, int32_t bz,
                   const MesherConfig& cfg,
                   const BlockRegistry& blocks,
                   const NeighborVoxelViews& neighbors) -> int32_t {
  int32_t lz = faceZ - 1; // blocks at the face level (air side)
  bool s1 = isSolidExcluding(voxels, cx-1, cy,   lz, cfg, blocks, bx, by, bz, neighbors);
  bool s2 = isSolidExcluding(voxels, cx,   cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  bool c  = isSolidExcluding(voxels, cx-1, cy-1, lz, cfg, blocks, bx, by, bz, neighbors);
  return calculateAO(s1, s2, c);
}

// Pre-compute the packed AO key for a single 1x1 face.
// Returns 8 bits: 4 corners × 2 bits each (AO values 0-3).
inline auto computeFaceAOPacked(
    const uint8_t* voxels, int32_t di, int32_t sl, int32_t u, int32_t v,
    const DirInfo& info, const MesherConfig& cfg, const BlockRegistry& blocks,
    const NeighborVoxelViews& neighbors) -> uint8_t {
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

} // namespace mesher
} // namespace voxel
