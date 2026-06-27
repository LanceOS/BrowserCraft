#include "EntityCollisions.hpp"
#include "world/ChunkCoords.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"
#include "engine/core/Config.hpp"
#include "engine/ecs/components/Components.hpp"
#include "world/terrain/TerrainCollision.hpp"
#include <algorithm>
#include <cmath>

namespace terrain {

EntityCollisions::EntityCollisions(World& world, const GameConfig& config)
  : m_world(world)
  , m_config(config)
{}

// ---------------------------------------------------------------------------
// AABB vs. world collision test
// ---------------------------------------------------------------------------
auto EntityCollisions::collidesAt(
    const glm::vec3& candidatePosition,
    const cmp::RigidBody& body) const -> bool
{
  const glm::vec3 minPoint = candidatePosition + body.aabbMin;
  const glm::vec3 maxPoint = candidatePosition + body.aabbMax;

  const int32_t chunkSize = m_config.chunkSize;

  // Check Terrain Mesh Triangles
  int32_t minCX = floorToChunk(static_cast<int32_t>(std::floor(minPoint.x)), chunkSize);
  int32_t maxCX = floorToChunk(static_cast<int32_t>(std::floor(maxPoint.x)), chunkSize);
  int32_t minCZ = floorToChunk(static_cast<int32_t>(std::floor(minPoint.z)), chunkSize);
  int32_t maxCZ = floorToChunk(static_cast<int32_t>(std::floor(maxPoint.z)), chunkSize);

  for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
    for (int32_t cx = minCX; cx <= maxCX; ++cx) {
      const Chunk* chunk = m_world.getChunk(cx, cz);
      if (chunk && chunk->terrainCollision && !chunk->terrainCollision->empty()) {
        if (chunk->terrainCollision->intersectsAABB(minPoint, maxPoint)) {
          return true;
        }
      }
    }
  }

  return false;
}

// ---------------------------------------------------------------------------
// Ground scanning
// ---------------------------------------------------------------------------
auto EntityCollisions::groundHeightAt(
    float worldX, float worldZ, int32_t startY) const -> int32_t
{
  const int32_t x = static_cast<int32_t>(std::floor(worldX));
  const int32_t z = static_cast<int32_t>(std::floor(worldZ));
  const int32_t cx = floorToChunk(x, m_config.chunkSize);
  const int32_t cz = floorToChunk(z, m_config.chunkSize);
  const Chunk* chunk = m_world.getChunk(cx, cz);
  if (!chunk) return -1;

  // Check smooth terrain mesh
  if (chunk->terrainCollision && !chunk->terrainCollision->empty()) {
    glm::vec3 origin(worldX, static_cast<float>(startY) + 1.0f, worldZ);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    glm::vec3 hitPos(0.0f);
    glm::vec3 hitNormal(0.0f);
    float hitDist = 0.0f;
    float maxDist = static_cast<float>(startY) + 2.0f;

    if (chunk->terrainCollision->raycast(origin, direction, maxDist, hitPos, hitNormal, hitDist)) {
      return static_cast<int32_t>(std::floor(hitPos.y));
    }
  }

  return -1;
}

// ---------------------------------------------------------------------------
// Fluid check
// ---------------------------------------------------------------------------
auto EntityCollisions::isFluidAt(float /*worldX*/, float /*worldY*/,
                                 float /*worldZ*/) const -> bool
{
  return false;
}

// ---------------------------------------------------------------------------
// Terrain check
// ---------------------------------------------------------------------------
auto EntityCollisions::hasTerrain() const -> bool {
  return m_world.hasTerrain();
}

// ---------------------------------------------------------------------------
// Push out of terrain
// ---------------------------------------------------------------------------
void EntityCollisions::pushOutOfTerrain(glm::vec3& position,
                                       const cmp::RigidBody& body) const
{
  int32_t maxIter = 256;
  while (collidesAt(position, body) && --maxIter > 0) {
    position.y += 0.05f;
  }
}

} // namespace terrain
