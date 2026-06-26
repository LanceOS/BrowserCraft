#include "EntityCollisions.hpp"
#include "world/ChunkCoords.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"
#include "engine/core/Config.hpp"
#include "engine/ecs/components/Components.hpp"
#include "world/terrain/TerrainCollision.hpp"
#include "world/BlockIds.hpp"
#include <algorithm>
#include <cmath>

namespace voxel {

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
  const int32_t worldHeight = m_config.worldHeight;

  // 1. Check Terrain Mesh Triangles first
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

  // 2. Check Block Grid (Dual System)
  int32_t minX = static_cast<int32_t>(std::floor(minPoint.x));
  int32_t maxX = static_cast<int32_t>(std::floor(maxPoint.x));
  int32_t minY = static_cast<int32_t>(std::floor(minPoint.y));
  int32_t maxY = static_cast<int32_t>(std::floor(maxPoint.y));
  int32_t minZ = static_cast<int32_t>(std::floor(minPoint.z));
  int32_t maxZ = static_cast<int32_t>(std::floor(maxPoint.z));

  constexpr int32_t kSentinel = int32_t(0x7FFFFFFF);
  const Chunk* cachedChunk = nullptr;
  int32_t cachedCX = kSentinel;
  int32_t cachedCZ = kSentinel;

  for (int32_t y = minY; y <= maxY; ++y) {
    if (y < 0 || y >= worldHeight) continue;
    for (int32_t z = minZ; z <= maxZ; ++z) {
      int32_t cz = floorToChunk(z, chunkSize);
      for (int32_t x = minX; x <= maxX; ++x) {
        int32_t cx = floorToChunk(x, chunkSize);

        if (cx != cachedCX || cz != cachedCZ) {
          cachedChunk = m_world.getChunk(cx, cz);
          cachedCX = cx;
          cachedCZ = cz;
        }
        if (!cachedChunk) continue;

        if (m_world.isSolidInChunk(x, y, z, *cachedChunk)) {
          bool hasTerrainMesh = (cachedChunk->terrainCollision && !cachedChunk->terrainCollision->empty());
          if (hasTerrainMesh) {
            uint8_t blockId = m_world.getBlockIdAt(x, y, z);
            bool isNatural = (blockId == BlockId::GRASS ||
                              blockId == BlockId::DIRT ||
                              blockId == BlockId::STONE ||
                              blockId == BlockId::SAND ||
                              blockId == BlockId::BEDROCK ||
                              blockId == BlockId::MOSSY_STONE);
            if (!isNatural) {
              return true;
            }
          } else {
            return true;
          }
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

  // 1. Check smooth terrain mesh first
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

  // 2. Fall back to block grid scanning
  int32_t y = std::clamp(startY, 0, m_config.worldHeight - 1);
  for (; y >= 0; --y) {
    if (m_world.isSolidInChunk(x, y, z, *chunk)) return y;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Fluid check
// ---------------------------------------------------------------------------
auto EntityCollisions::isFluidAt(float worldX, float worldY,
                                 float worldZ) const -> bool
{
  int32_t x = static_cast<int32_t>(std::floor(worldX));
  int32_t y = static_cast<int32_t>(std::max(0, static_cast<int32_t>(std::floor(worldY))));
  int32_t z = static_cast<int32_t>(std::floor(worldZ));
  return m_world.isFluid(x, y, z);
}

// ---------------------------------------------------------------------------
// Terrain check
// ---------------------------------------------------------------------------
auto EntityCollisions::hasTerrain() const -> bool {
  return m_world.hasTerrain();
}

// ---------------------------------------------------------------------------
// Push out of blocks
// ---------------------------------------------------------------------------
void EntityCollisions::pushOutOfBlocks(glm::vec3& position,
                                       const cmp::RigidBody& body) const
{
  int32_t maxIter = 256;
  while (collidesAt(position, body) && --maxIter > 0) {
    position.y += 0.05f;
  }
}

} // namespace voxel
