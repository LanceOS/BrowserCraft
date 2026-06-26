#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/workers/mesher/GreedyMesher.hpp"
#include "engine/workers/mesher/LightPropagator.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockDefinition.hpp"
#include "world/BlockIds.hpp"
#include <vector>
#include <cmath>

using namespace voxel;

// ======================================================================
// Helper: register a test block with distinct texture indices per face
// ======================================================================
static auto registerTestBlock(BlockRegistry& reg, uint8_t id,
                               const char* name,
                               uint8_t texTop,
                               uint8_t texBottom,
                               uint8_t texSide,
                               bool opaque = true,
                               uint8_t lightEmission = 0) -> void {
  BlockDefinition def;
  def.id = id;
  def.name = name;
  def.textures.top = texTop;
  def.textures.bottom = texBottom;
  def.textures.side = texSide;
  def.material.opaque = opaque;
  if (!opaque) {
    def.material.transparent = true;
  }
  def.material.lightEmission = lightEmission;
  reg.register_(std::move(def));
}

// ======================================================================
// Constants for vertex layout
// ======================================================================
static constexpr int32_t VP_STRIDE = 10;  // floats per vertex
// Offsets within a vertex
static constexpr int32_t OFF_POS_X = 0;
static constexpr int32_t OFF_POS_Y = 1;
static constexpr int32_t OFF_POS_Z = 2;
static constexpr int32_t OFF_NRM_X = 3;
static constexpr int32_t OFF_NRM_Y = 4;
static constexpr int32_t OFF_NRM_Z = 5;
static constexpr int32_t OFF_UV_U  = 6;
static constexpr int32_t OFF_UV_V  = 7;
static constexpr int32_t OFF_TEX   = 8;
static constexpr int32_t OFF_LIGHT = 9;

// ======================================================================
// Test: Single isolated block produces correct texture layers per face
// ======================================================================
TEST_CASE("GreedyMesher assigns correct texture layer per face direction",
          "[mesher][face]") {
  // ---- Setup ----
  BlockRegistry reg(256);
  // Use distinct texture indices so each face is clearly identifiable
  // Grass-like: top=11, bottom=22, side=33
  registerTestBlock(reg, 1, "TestBlock", 11, 22, 33);

  // Small voxel grid: 3×3×3 with one block at center
  constexpr int32_t SX = 3, SY = 3, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  // Place block at (1,1,1)
  voxels[(1 * SZ + 1) * SX + 1] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);
  REQUIRE(ok);
  REQUIRE(vertexCount > 0);
  REQUIRE(indexCount > 0);

  const uint32_t numVerts = vertexCount;
  REQUIRE(numVerts % 4 == 0); // should be complete quads

  const uint32_t numQuads = numVerts / 4;

  // ---- Check each quad ----
  // Gather per-face stats: count quads with each normal direction
  int faceCounts[6] = {0, 0, 0, 0, 0, 0};

  for (uint32_t qi = 0; qi < numQuads; ++qi) {
    // First vertex of this quad gives us normal and texLayer
    const float* v0 = &vertices[qi * 4 * VP_STRIDE];

    float nx = v0[OFF_NRM_X];
    float ny = v0[OFF_NRM_Y];
    float nz = v0[OFF_NRM_Z];
    float tex = v0[OFF_TEX];

    // Determine face direction from normal
    int di = -1;
    if      (nx > 0.5f) di = 0; // X+
    else if (nx < -0.5f) di = 1; // X-
    else if (ny > 0.5f) di = 2; // Y+
    else if (ny < -0.5f) di = 3; // Y-
    else if (nz > 0.5f) di = 4; // Z+
    else if (nz < -0.5f) di = 5; // Z-

    REQUIRE((di >= 0));

    faceCounts[di]++;

    // Expected texture layer for this face direction
    uint8_t expectedTex;
    switch (di) {
      case 0: case 1: expectedTex = 33; break; // side
      case 2:         expectedTex = 11; break; // top
      case 3:         expectedTex = 22; break; // bottom
      case 4: case 5: expectedTex = 33; break; // side
      default:        expectedTex = 0;  break;
    }

    CAPTURE(di, nx, ny, nz, tex, expectedTex);
    CHECK(static_cast<int>(tex + 0.5f) == expectedTex);
  }

  // A single isolated block should produce exactly 6 face quads
  // (X+, X-, Y+, Y-, Z+, Z-), one per direction
  CHECK(numQuads == 6);
  for (int di = 0; di < 6; ++di) {
    CAPTURE(di);
    CHECK(faceCounts[di] == 1);
  }
}

TEST_CASE("GreedyMesher maxVertices is counted in vertices, not floats",
          "[mesher][capacity]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "TestBlock", 11, 22, 33);

  constexpr int32_t SX = 3, SY = 3, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);
  voxels[(1 * SZ + 1) * SX + 1] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 24; // 6 quads * 4 vertices
  cfg.maxIndices = 36;  // 6 quads * 6 indices
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);

  CHECK(ok);
  CHECK(vertexCount == 24);
  CHECK(indexCount == 36);
}

TEST_CASE("Mesh capacity estimate matches a single exposed block",
          "[mesher][capacity]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "TestBlock", 11, 22, 33);

  constexpr int32_t SX = 3, SY = 3, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);
  voxels[(1 * SZ + 1) * SX + 1] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;

  auto hint = mesher::estimateMeshCapacity(voxels.data(), reg, cfg);
  CHECK(hint.quadCount == 6);
  CHECK(hint.vertexCount == 24);
  CHECK(hint.indexCount == 36);
}

TEST_CASE("LightPropagator fills open columns with sky light",
          "[mesher][light]") {
  BlockRegistry reg(256);

  mesher::MesherConfig cfg;
  cfg.sizeX = 1;
  cfg.sizeY = 4;
  cfg.sizeZ = 1;

  std::vector<uint8_t> voxels(4, 0);
  std::vector<uint8_t> light(4, 0);

  mesher::calculateLighting(voxels.data(), light.data(), reg, cfg);

  for (uint8_t packed : light) {
    CHECK(mesher::skyLightNibble(packed) == 15);
    CHECK(mesher::blockLightNibble(packed) == 0);
  }
}

TEST_CASE("LightPropagator propagates emitted block light",
          "[mesher][light]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "Glow", 0, 0, 0, false, 15);

  constexpr int32_t SX = 5, SY = 5, SZ = 5;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);
  voxels[(2 * SZ + 2) * SX + 2] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;

  mesher::calculateLighting(voxels.data(), light.data(), reg, cfg);

  CHECK(mesher::blockLightNibble(light[(2 * SZ + 2) * SX + 2]) == 15);
  CHECK(mesher::blockLightNibble(light[(2 * SZ + 2) * SX + 3]) == 14);
  CHECK(mesher::blockLightNibble(light[(2 * SZ + 2) * SX + 4]) == 13);
}

TEST_CASE("GreedyMesher keeps border faces fully lit when chunk light is uniform",
          "[mesher][light][border]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "TestBlock", 11, 22, 33);

  constexpr int32_t SX = 2, SY = 2, SZ = 2;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, mesher::packVoxelLight(15, 0));
  voxels[(0 * SZ + 0) * SX + 0] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);
  REQUIRE(ok);
  REQUIRE(vertexCount > 0);

  for (uint32_t vi = 0; vi < vertexCount; vi += VP_STRIDE) {
    const uint32_t packed = static_cast<uint32_t>(vertices[vi + OFF_LIGHT] + 0.5f);
    CAPTURE(vi, packed);
    CHECK((packed & 0x0Fu) == 15u);
    CHECK(((packed >> 4u) & 0x0Fu) == 0u);
  }
}

// ======================================================================
// Test: UV V coordinate maps to world Y on all side faces
// ======================================================================
TEST_CASE("GreedyMesher UV V maps to world Y on side faces",
          "[mesher][face][uv]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "TestBlock", 11, 22, 33);

  // Use a slightly taller grid (3×4×3) so we can verify V follows Y
  constexpr int32_t SX = 3, SY = 4, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  // Place block at (1,1,1)
  voxels[(1 * SZ + 1) * SX + 1] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);
  REQUIRE(ok);

  const uint32_t numVerts = vertexCount;
  REQUIRE(numVerts % 4 == 0);

  const uint32_t numQuads = numVerts / 4;

  // Check each side-face quad (X+, X-, Z+, Z-)
  for (uint32_t qi = 0; qi < numQuads; ++qi) {
    const float* v = &vertices[qi * 4 * VP_STRIDE];

    float nx = v[OFF_NRM_X];
    float ny = v[OFF_NRM_Y];
    float nz = v[OFF_NRM_Z];

    // Skip top/bottom faces (normal along Y)
    if (std::abs(ny) > 0.5f) continue;

    // For side faces, the four vertices of the quad follow this order:
    //   v[0] = bl (bottom-left)   → lowest u, lowest v
    //   v[1] = br (bottom-right)  → highest u, lowest v
    //   v[2] = tl (top-left)      → lowest u, highest v
    //   v[3] = tr (top-right)     → highest u, highest v
    //
    // The V coordinate (uv[1]) should map to world Y.
    // So bl and br should have V=0 and Y=minY,
    // while tl and tr should have V=rh and Y=maxY.

    float bl_y = v[OFF_POS_Y];
    float bl_v = v[OFF_UV_V];
    float br_y = v[1 * VP_STRIDE + OFF_POS_Y];
    float br_v = v[1 * VP_STRIDE + OFF_UV_V];
    float tl_y = v[2 * VP_STRIDE + OFF_POS_Y];
    float tl_v = v[2 * VP_STRIDE + OFF_UV_V];
    float tr_y = v[3 * VP_STRIDE + OFF_POS_Y];
    float tr_v = v[3 * VP_STRIDE + OFF_UV_V];

    // Bottom vertices (bl, br) should have same Y and V
    CHECK(bl_y == br_y);
    CHECK(bl_v == br_v);

    // Top vertices (tl, tr) should have same Y and V
    CHECK(tl_y == tr_y);
    CHECK(tl_v == tr_v);

    // Top Y should be > bottom Y
    CHECK(tl_y > bl_y);

    // Top V should be > bottom V (V maps to Y)
    CHECK(tl_v > bl_v);

    // The difference in Y should equal the difference in V
    // (for a single block, V goes from 0 to 1)
    CHECK(tl_y - bl_y == tl_v - bl_v);
  }
}

TEST_CASE("GreedyMesher side faces interpolate light across world Y",
          "[mesher][light][uv]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "TestBlock", 11, 22, 33);

  constexpr int32_t SX = 3, SY = 4, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);
  voxels[(1 * SZ + 1) * SX + 1] = 1;

  for (int32_t y = 0; y < SY; ++y) {
    for (int32_t z = 0; z < SZ; ++z) {
      for (int32_t x = 0; x < SX; ++x) {
        light[(y * SZ + z) * SX + x] = mesher::packVoxelLight(static_cast<uint8_t>(y), 0);
      }
    }
  }

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);
  REQUIRE(ok);

  bool checkedSideFace = false;
  const uint32_t numQuads = vertexCount / 4;
  for (uint32_t qi = 0; qi < numQuads; ++qi) {
    const float* v = &vertices[qi * 4 * VP_STRIDE];
    float ny = v[OFF_NRM_Y];
    if (std::abs(ny) > 0.5f) continue;

    const uint32_t blPacked = static_cast<uint32_t>(v[OFF_LIGHT] + 0.5f);
    const uint32_t brPacked = static_cast<uint32_t>(v[1 * VP_STRIDE + OFF_LIGHT] + 0.5f);
    const uint32_t tlPacked = static_cast<uint32_t>(v[2 * VP_STRIDE + OFF_LIGHT] + 0.5f);
    const uint32_t trPacked = static_cast<uint32_t>(v[3 * VP_STRIDE + OFF_LIGHT] + 0.5f);

    const int blSky = static_cast<int>(blPacked & 0x0Fu);
    const int brSky = static_cast<int>(brPacked & 0x0Fu);
    const int tlSky = static_cast<int>(tlPacked & 0x0Fu);
    const int trSky = static_cast<int>(trPacked & 0x0Fu);

    CAPTURE(qi, blSky, brSky, tlSky, trSky);
    CHECK(tlSky > blSky);
    CHECK(trSky > brSky);
    checkedSideFace = true;
  }

  CHECK(checkedSideFace);
}

// ======================================================================
// Test: Multiple blocks with distinct textures all map correctly
// ======================================================================
TEST_CASE("GreedyMesher handles multiple block types with distinct textures",
          "[mesher][face]") {
  BlockRegistry reg(256);
  // Register blocks with different texture sets
  registerTestBlock(reg, 1, "BlockA", 10, 20, 30);
  registerTestBlock(reg, 2, "BlockB", 40, 50, 60);
  registerTestBlock(reg, 3, "BlockC", 70, 80, 90);

  constexpr int32_t SX = 5, SY = 5, SZ = 5;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  // Place three blocks in a row along X, separated by air
  // BlockA at (1,1,1), BlockB at (3,1,1), BlockC at (1,3,1)
  voxels[(1 * SZ + 1) * SX + 1] = 1;  // BlockA
  voxels[(1 * SZ + 1) * SX + 3] = 2;  // BlockB
  voxels[(3 * SZ + 1) * SX + 1] = 3;  // BlockC

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);
  REQUIRE(ok);
  REQUIRE(vertexCount > 0);

  // For each vertex, determine block type from position and verify
  // texture layer matches expected
  const uint32_t numVerts = vertexCount;
  for (uint32_t vi = 0; vi + VP_STRIDE <= numVerts; vi += VP_STRIDE) {
    const float* v = &vertices[vi];
    float px = v[OFF_POS_X];
    float py = v[OFF_POS_Y];
    float pz = v[OFF_POS_Z];
    float nx = v[OFF_NRM_X];
    float ny = v[OFF_NRM_Y];
    float nz = v[OFF_NRM_Z];
    float tex = v[OFF_TEX];
    int texInt = static_cast<int>(tex + 0.5f);

    // Determine which block this vertex belongs to
    // The face is at the block boundary, so the block is one step
    // opposite the normal direction from the face position.
    int blockX = static_cast<int>(std::floor(px + 0.5f)) - (nx > 0.5f ? 1 : 0);
    // For negative normals, the face is at min coord of the block
    if (nx < -0.5f) blockX = static_cast<int>(std::floor(px + 0.5f));
    if (ny > 0.5f) blockX = static_cast<int>(std::floor(px + 0.5f));
    if (ny < -0.5f) blockX = static_cast<int>(std::floor(px + 0.5f));
    if (nz > 0.5f) blockX = static_cast<int>(std::floor(px + 0.5f));
    if (nz < -0.5f) blockX = static_cast<int>(std::floor(px + 0.5f));

    int blockY = static_cast<int>(std::floor(py + 0.5f));
    if (nx > 0.5f) blockY = static_cast<int>(std::floor(py + 0.5f));
    if (nx < -0.5f) blockY = static_cast<int>(std::floor(py + 0.5f));
    if (ny > 0.5f) blockY = static_cast<int>(std::floor(py)) - 1;
    if (ny < -0.5f) blockY = static_cast<int>(std::floor(py + 0.5f));
    if (nz > 0.5f) blockY = static_cast<int>(std::floor(py + 0.5f));
    if (nz < -0.5f) blockY = static_cast<int>(std::floor(py + 0.5f));

    int blockZ = static_cast<int>(std::floor(pz + 0.5f));
    if (nx > 0.5f) blockZ = static_cast<int>(std::floor(pz + 0.5f));
    if (nx < -0.5f) blockZ = static_cast<int>(std::floor(pz + 0.5f));
    if (ny > 0.5f) blockZ = static_cast<int>(std::floor(pz + 0.5f));
    if (ny < -0.5f) blockZ = static_cast<int>(std::floor(pz + 0.5f));
    if (nz > 0.5f) blockZ = static_cast<int>(std::floor(pz)) - 1;
    if (nz < -0.5f) blockZ = static_cast<int>(std::floor(pz + 0.5f));

    // Look up block ID at that position
    if (blockX < 0 || blockX >= SX || blockY < 0 || blockY >= SY || blockZ < 0 || blockZ >= SZ)
      continue;
    uint8_t blockId = voxels[(blockY * SZ + blockZ) * SX + blockX];
    if (blockId == 0) continue;

    const auto* def = reg.tryGet(blockId);
    REQUIRE(def != nullptr);

    // Determine face direction
    int di = -1;
    if      (nx > 0.5f) di = 0;
    else if (nx < -0.5f) di = 1;
    else if (ny > 0.5f) di = 2;
    else if (ny < -0.5f) di = 3;
    else if (nz > 0.5f) di = 4;
    else if (nz < -0.5f) di = 5;

    REQUIRE(di >= 0);

    // Expected texture
    uint8_t expTex;
    switch (di) {
      case 0: case 1: expTex = def->textures.side;   break;
      case 2:         expTex = def->textures.top;    break;
      case 3:         expTex = def->textures.bottom; break;
      case 4: case 5: expTex = def->textures.side;   break;
      default:        expTex = 0; break;
    }

    CAPTURE(vi, blockId, blockX, blockY, blockZ, di, texInt, expTex);
    CHECK(texInt == expTex);
  }
}

// ======================================================================
// Test: Non-opaque blocks produce transparent mesh flag and opaque geometry flag
// ======================================================================
TEST_CASE("GreedyMesher reports opaque and transparent geometry independently",
          "[mesher][face]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "Opaque",   0, 0, 0, true);
  registerTestBlock(reg, 2, "Transparent", 1, 1, 1, false);

  constexpr int32_t SX = 3, SY = 3, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  // Place opaque block
  voxels[(1 * SZ + 1) * SX + 1] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;
  bool hasTransparent = false;
  bool hasOpaque = false;

  // Opaque block only → should not set transparent flag
  mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                      vertices.data(), indices.data(),
                      vertexCount, indexCount, &hasTransparent, &hasOpaque);
  CHECK(hasTransparent == false);
  CHECK(hasOpaque == true);

  // Add transparent block
  voxels[(1 * SZ + 1) * SX + 1] = 2;
  hasTransparent = false;
  hasOpaque = false;
  mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                      vertices.data(), indices.data(),
                      vertexCount, indexCount, &hasTransparent, &hasOpaque);
  CHECK(hasTransparent == true);
  CHECK(hasOpaque == false);
}

// ======================================================================
// Test: Face culling — opaque blocks don't produce faces between them
// ======================================================================
TEST_CASE("GreedyMesher culls faces between adjacent opaque blocks",
          "[mesher][face]") {
  BlockRegistry reg(256);
  // Use DIFFERENT block IDs so greedy mesher doesn't merge coplanar faces
  registerTestBlock(reg, 1, "BlockA", 0, 0, 0, true);
  registerTestBlock(reg, 2, "BlockB", 1, 1, 1, true);

  constexpr int32_t SX = 3, SY = 3, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  // Two adjacent opaque blocks at (1,1,1) and (2,1,1)
  voxels[(1 * SZ + 1) * SX + 1] = 1;
  voxels[(1 * SZ + 1) * SX + 2] = 2;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                      vertices.data(), indices.data(),
                      vertexCount, indexCount);

  // Two adjacent opaque blocks with DIFFERENT IDs should have 10 faces total
  // (6 per block = 12, minus 2 shared faces between them = 10)
  // Each face is a quad with 4 vertices
  const uint32_t numVerts = vertexCount;
  const uint32_t expectedFaces = 10;
  CHECK(numVerts / 4 == expectedFaces);
}

// ======================================================================
// Test: Stone block (uniform texture) produces same tex on all faces
// ======================================================================
TEST_CASE("GreedyMesher uniform block has same texture on all faces",
          "[mesher][face]") {
  BlockRegistry reg(256);
  // Stone uses same texture index for all faces (like "all" in blocks.json)
  registerTestBlock(reg, 1, "Stone", 7, 7, 7, true);

  constexpr int32_t SX = 3, SY = 3, SZ = 3;
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  voxels[(1 * SZ + 1) * SX + 1] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                      vertices.data(), indices.data(),
                      vertexCount, indexCount);

  // All vertices should have texLayer = 7
  for (uint32_t vi = 0; vi + VP_STRIDE <= vertexCount; vi += VP_STRIDE) {
    float tex = vertices[vi + OFF_TEX];
    CAPTURE(vi, tex);
    CHECK(static_cast<int>(tex + 0.5f) == 7);
  }
}

// ======================================================================
// Test: Solid cube of same block type produces only exterior faces
// ======================================================================
TEST_CASE("GreedyMesher solid cube produces only exterior faces with correct normals",
          "[mesher][face][solid]") {
  BlockRegistry reg(256);
  registerTestBlock(reg, 1, "Stone", 7, 7, 7, true);

  constexpr int32_t SZ = 8, SY = 8, SX = 8;  // Note: SZ then SY then SX to match mesher config order
  std::vector<uint8_t> voxels(SX * SY * SZ, 0);
  std::vector<uint8_t> light(SX * SY * SZ, 0);

  // Fill the entire cube with stone (block ID 1)
  for (int32_t y = 0; y < SY; ++y)
    for (int32_t z = 0; z < SZ; ++z)
      for (int32_t x = 0; x < SX; ++x)
        voxels[(y * SZ + z) * SX + x] = 1;

  mesher::MesherConfig cfg;
  cfg.sizeX = SX;
  cfg.sizeY = SY;
  cfg.sizeZ = SZ;
  cfg.maxVertices = 5000;
  cfg.maxIndices = 10000;
  cfg.strideFloats = VP_STRIDE;

  std::vector<float> vertices(cfg.maxVertices * cfg.strideFloats, 0.0f);
  std::vector<uint32_t> indices(cfg.maxIndices, 0);
  uint32_t vertexCount = 0, indexCount = 0;

  bool ok = mesher::greedyMesh(voxels.data(), light.data(), reg, cfg,
                                vertices.data(), indices.data(),
                                vertexCount, indexCount);
  REQUIRE(ok);
  REQUIRE(vertexCount > 0);
  REQUIRE(indexCount > 0);

  // A solid 8x8x8 cube of the same block type should produce exactly 6 quads
  // (one per exterior face: X+, X-, Y+, Y-, Z+, Z-), each greedily merged
  // into a single large quad.
  const uint32_t numVerts = vertexCount;
  REQUIRE(numVerts % 4 == 0);
  const uint32_t numQuads = numVerts / 4;
  INFO("numQuads=" << numQuads << " (expected 6)");

  // Count faces per direction
  int faceCounts[6] = {0, 0, 0, 0, 0, 0};
  for (uint32_t qi = 0; qi < numQuads; ++qi) {
    const float* v0 = &vertices[qi * 4 * VP_STRIDE];
    float nx = v0[OFF_NRM_X];
    float ny = v0[OFF_NRM_Y];
    float nz = v0[OFF_NRM_Z];

    int di = -1;
    if      (nx > 0.5f) di = 0;
    else if (nx < -0.5f) di = 1;
    else if (ny > 0.5f) di = 2;
    else if (ny < -0.5f) di = 3;
    else if (nz > 0.5f) di = 4;
    else if (nz < -0.5f) di = 5;

    REQUIRE((di >= 0));
    faceCounts[di]++;
  }

  // Each direction should have exactly 1 quad
  for (int di = 0; di < 6; ++di) {
    INFO("Direction " << di << " face count: " << faceCounts[di]);
    CHECK(faceCounts[di] == 1);
  }

  // Check that each quad has the correct corner positions
  for (uint32_t qi = 0; qi < numQuads; ++qi) {
    const float* v = &vertices[qi * 4 * VP_STRIDE];
    float nx = v[OFF_NRM_X];
    float ny = v[OFF_NRM_Y];
    float nz = v[OFF_NRM_Z];

    // Get all 4 vertex positions of this quad
    float px[4], py[4], pz[4];
    for (int ci = 0; ci < 4; ++ci) {
      px[ci] = v[ci * VP_STRIDE + OFF_POS_X];
      py[ci] = v[ci * VP_STRIDE + OFF_POS_Y];
      pz[ci] = v[ci * VP_STRIDE + OFF_POS_Z];
    }

    if (nx > 0.5f) {
      // X+ face: all x should equal SX (= 8), y and z span [0, SZ] and [0, SY]
      for (int ci = 0; ci < 4; ++ci) CHECK(px[ci] == Catch::Approx(static_cast<float>(SX)));
    } else if (nx < -0.5f) {
      // X- face: all x should equal 0
      for (int ci = 0; ci < 4; ++ci) CHECK(px[ci] == Catch::Approx(0.0f));
    } else if (ny > 0.5f) {
      // Y+ face: all y should equal SY (= 8)
      for (int ci = 0; ci < 4; ++ci) CHECK(py[ci] == Catch::Approx(static_cast<float>(SY)));
    } else if (ny < -0.5f) {
      // Y- face: all y should equal 0
      for (int ci = 0; ci < 4; ++ci) CHECK(py[ci] == Catch::Approx(0.0f));
    } else if (nz > 0.5f) {
      // Z+ face: all z should equal SZ (= 8)
      for (int ci = 0; ci < 4; ++ci) CHECK(pz[ci] == Catch::Approx(static_cast<float>(SZ)));
    } else if (nz < -0.5f) {
      // Z- face: all z should equal 0
      for (int ci = 0; ci < 4; ++ci) CHECK(pz[ci] == Catch::Approx(0.0f));
    }
  }

  // Verify that all vertices have the correct normal (matches the per-vertex
  // normal set by the mesher, not the geometric winding normal).
  for (uint32_t vi = 0; vi + VP_STRIDE <= vertexCount; vi += VP_STRIDE) {
    const float* v = &vertices[vi];
    float nx = v[OFF_NRM_X];
    float ny = v[OFF_NRM_Y];
    float nz = v[OFF_NRM_Z];

    // Normal should be one of the 6 axis directions
    bool validNormal = false;
    if (std::abs(nx - 1.0f) < 0.01f && std::abs(ny) < 0.01f && std::abs(nz) < 0.01f) validNormal = true;
    if (std::abs(nx + 1.0f) < 0.01f && std::abs(ny) < 0.01f && std::abs(nz) < 0.01f) validNormal = true;
    if (std::abs(ny - 1.0f) < 0.01f && std::abs(nx) < 0.01f && std::abs(nz) < 0.01f) validNormal = true;
    if (std::abs(ny + 1.0f) < 0.01f && std::abs(nx) < 0.01f && std::abs(nz) < 0.01f) validNormal = true;
    if (std::abs(nz - 1.0f) < 0.01f && std::abs(nx) < 0.01f && std::abs(ny) < 0.01f) validNormal = true;
    if (std::abs(nz + 1.0f) < 0.01f && std::abs(nx) < 0.01f && std::abs(ny) < 0.01f) validNormal = true;
    CHECK(validNormal);
  }
}
