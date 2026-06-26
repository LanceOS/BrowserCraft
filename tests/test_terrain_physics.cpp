#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include "world/terrain/TerrainCollision.hpp"
#include "world/terrain/TerrainRaycast.hpp"
#include "world/World.hpp"
#include "world/BlockRegistry.hpp"
#include "engine/core/Config.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/collision/EntityCollisions.hpp"
#include "MockWorker.hpp"
#include <memory>
#include <vector>

namespace voxel {

// Helper to declare rayTriangleIntersect in test if not in header
extern bool rayTriangleIntersect(const glm::vec3& origin, const glm::vec3& dir,
                                 const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                 float& t, glm::vec3& normal);

// Helper to declare intersectsAABBTriangle in test if not in header
extern bool intersectsAABBTriangle(const glm::vec3& boxCenter, const glm::vec3& boxHalfSize,
                                   const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);

TEST_CASE("Ray-Triangle Intersection (Möller-Trumbore)", "[terrain_physics]") {
  glm::vec3 v0(0.0f, 0.0f, 0.0f);
  glm::vec3 v1(1.0f, 0.0f, 0.0f);
  glm::vec3 v2(0.0f, 0.0f, 1.0f);

  SECTION("Direct hit") {
    glm::vec3 origin(0.25f, 1.0f, 0.25f);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    float t = 0.0f;
    glm::vec3 normal(0.0f);
    bool hit = rayTriangleIntersect(origin, direction, v0, v1, v2, t, normal);
    REQUIRE(hit);
    REQUIRE(t == 1.0f);
    REQUIRE(normal.y == 1.0f);
  }

  SECTION("Ray misses") {
    glm::vec3 origin(2.0f, 1.0f, 2.0f);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    float t = 0.0f;
    glm::vec3 normal(0.0f);
    bool hit = rayTriangleIntersect(origin, direction, v0, v1, v2, t, normal);
    REQUIRE_FALSE(hit);
  }

  SECTION("Parallel ray") {
    glm::vec3 origin(0.25f, 0.0f, 0.25f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);
    float t = 0.0f;
    glm::vec3 normal(0.0f);
    bool hit = rayTriangleIntersect(origin, direction, v0, v1, v2, t, normal);
    REQUIRE_FALSE(hit);
  }
}

TEST_CASE("AABB-Triangle Intersection (SAT)", "[terrain_physics]") {
  glm::vec3 v0(0.0f, 0.0f, 0.0f);
  glm::vec3 v1(2.0f, 0.0f, 0.0f);
  glm::vec3 v2(0.0f, 0.0f, 2.0f);

  SECTION("AABB overlaps triangle") {
    glm::vec3 center(0.5f, 0.0f, 0.5f);
    glm::vec3 halfSize(0.5f, 0.5f, 0.5f);
    bool overlap = intersectsAABBTriangle(center, halfSize, v0, v1, v2);
    REQUIRE(overlap);
  }

  SECTION("AABB does not overlap") {
    glm::vec3 center(5.0f, 5.0f, 5.0f);
    glm::vec3 halfSize(1.0f, 1.0f, 1.0f);
    bool overlap = intersectsAABBTriangle(center, halfSize, v0, v1, v2);
    REQUIRE_FALSE(overlap);
  }
}

TEST_CASE("Terrain BVH Construction and Query", "[terrain_physics]") {
  TerrainChunkCollision collision;
  
  // Create a simple ramp mesh: 2 triangles forming a quad from (0,0,0) to (2,2,2)
  std::vector<TerrainTriangle> triangles = {
    { glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 0.0f), glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(-0.707f, 0.707f, 0.0f) },
    { glm::vec3(2.0f, 2.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(-0.707f, 0.707f, 0.0f) }
  };

  collision.build(std::move(triangles));

  REQUIRE_FALSE(collision.empty());
  REQUIRE(collision.getNodes().size() > 0);

  SECTION("AABB collision test") {
    // AABB intersecting the ramp
    glm::vec3 boxMin(0.5f, 0.0f, 0.5f);
    glm::vec3 boxMax(1.5f, 1.5f, 1.5f);
    REQUIRE(collision.intersectsAABB(boxMin, boxMax));

    // AABB completely above the ramp
    glm::vec3 boxMinAbove(0.0f, 5.0f, 0.0f);
    glm::vec3 boxMaxAbove(1.0f, 6.0f, 1.0f);
    REQUIRE_FALSE(collision.intersectsAABB(boxMinAbove, boxMaxAbove));
  }

  SECTION("Raycast test") {
    glm::vec3 origin(1.0f, 5.0f, 1.0f);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    glm::vec3 hitPos(0.0f);
    glm::vec3 hitNormal(0.0f);
    float hitDist = 0.0f;

    bool hit = collision.raycast(origin, direction, 10.0f, hitPos, hitNormal, hitDist);
    REQUIRE(hit);
    // At x=1, on a ramp from y=0 (x=0) to y=2 (x=2), y should be 1.0
    REQUIRE(hitPos.y == Catch::Approx(1.0f));
    REQUIRE(hitDist == Catch::Approx(4.0f));
  }
}

static auto makeTestConfig() -> GameConfig {
  GameConfig cfg{};
  cfg.chunkSize = 16;
  cfg.worldHeight = 256;
  cfg.renderDistance = 2;
  cfg.worldSeed = 42;
  cfg.maxVertsPerChunk = 10000;
  cfg.maxIndicesPerChunk = 20000;
  cfg.vertexStrideFloats = 10;
  return cfg;
}

static auto makeDims(const GameConfig& cfg) -> ChunkDimensions {
  return {
    cfg.chunkSize,
    cfg.worldHeight,
    cfg.chunkSize,
    cfg.maxVertsPerChunk,
    cfg.maxIndicesPerChunk,
    cfg.vertexStrideFloats,
  };
}

TEST_CASE("EntityCollisions Dual System Integration", "[terrain_physics]") {
  auto cfg = makeTestConfig();
  auto pool = SharedPool::create(16, makeDims(cfg));
  BlockRegistry blocks(256);
  // Register grass (1) as solid natural block, planks (6) as solid placed block
  BlockDefinition grassDef{.id = 1, .name = "grass"};
  grassDef.material.opaque = true;
  blocks.register_(std::move(grassDef));

  BlockDefinition planksDef{.id = 6, .name = "planks"};
  planksDef.material.opaque = true;
  blocks.register_(std::move(planksDef));

  TestChunkWorker worker;
  World world(*pool, blocks, cfg, worker, nullptr);

  // Initialize world and load center chunk
  world.update(glm::vec3(0.0f, 64.0f, 0.0f));
  auto* chunk = world.getChunkMut(0, 0);
  REQUIRE(chunk != nullptr);

  // Simulate successful mesh completion by manually creating and attaching a terrain collision object
  auto terrainCol = std::make_shared<TerrainChunkCollision>();
  
  // Create a flat terrain mesh at y=10.0f across the chunk
  std::vector<TerrainTriangle> triangles;
  // Two triangles covering the 16x16 chunk area at y=10
  triangles.push_back({ glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(16.0f, 10.0f, 0.0f), glm::vec3(0.0f, 10.0f, 16.0f), glm::vec3(0.0f, 1.0f, 0.0f) });
  triangles.push_back({ glm::vec3(16.0f, 10.0f, 0.0f), glm::vec3(16.0f, 10.0f, 16.0f), glm::vec3(0.0f, 10.0f, 16.0f), glm::vec3(0.0f, 1.0f, 0.0f) });
  terrainCol->build(std::move(triangles));
  chunk->terrainCollision = terrainCol;

  // Let's set some voxels in the chunk:
  // a natural block at (5, 10, 5) -> should be ignored because we have a terrain mesh
  // a placed block at (5, 12, 5) -> should NOT be ignored
  auto slot = pool->view(chunk->slotIndex);
  // flat index = (y * sz + z) * sx + x
  int32_t naturalIdx = (10 * 16 + 5) * 16 + 5;
  int32_t placedIdx = (12 * 16 + 5) * 16 + 5;
  slot.voxels[naturalIdx] = 1; // grass
  slot.voxels[placedIdx] = 6;  // planks

  EntityCollisions collisions(world, cfg);
  cmp::RigidBody body;
  body.aabbMin = glm::vec3(-0.3f, 0.0f, -0.3f);
  body.aabbMax = glm::vec3(0.3f, 1.8f, 0.3f);

  SECTION("Collision with smooth terrain mesh") {
    // Player is standing at y=9.5 (penetrating the terrain mesh at y=10.0) -> should collide!
    glm::vec3 pos(8.0f, 9.5f, 8.0f);
    REQUIRE(collisions.collidesAt(pos, body));

    // Player is standing completely above the terrain mesh at y=10.5 -> should not collide!
    glm::vec3 posAbove(8.0f, 10.5f, 8.0f);
    REQUIRE_FALSE(collisions.collidesAt(posAbove, body));
  }

  SECTION("Collision with placed blocks vs natural blocks in meshed chunk") {
    // Player standing at (5.5, 9.5, 5.5) which overlaps the natural grass voxel at (5,10,5),
    // but the terrain mesh is at y=10.0. If we only test the mesh, it collides because y=9.5 penetrates y=10.0.
    // If we test a position above the terrain mesh but overlapping the natural voxel:
    // e.g. position y=10.1 (so feet are at 10.1, head at 11.9, overlapping voxel y=10).
    // In this case, the natural voxel should be ignored!
    glm::vec3 posOverlapNatural(5.5f, 10.1f, 5.5f);
    REQUIRE_FALSE(collisions.collidesAt(posOverlapNatural, body));

    // However, if we overlap the placed planks voxel at (5, 12, 5):
    // e.g. position y=11.5 (so feet are at 11.5, body overlaps y=12).
    // The placed block at y=12 should collide!
    glm::vec3 posOverlapPlaced(5.5f, 11.5f, 5.5f);
    REQUIRE(collisions.collidesAt(posOverlapPlaced, body));
  }

  SECTION("Ground height check") {
    // Checking ground height at (8.0, 8.0) scanning down from y=20
    int32_t height = collisions.groundHeightAt(8.0f, 8.0f, 20);
    // Should hit the terrain mesh at y=10, so return 10 (floor of 10.0)
    REQUIRE(height == 10);
  }
}

} // namespace voxel
