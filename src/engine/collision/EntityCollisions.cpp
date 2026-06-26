#include "EntityCollisions.hpp"
#include "world/ChunkCoords.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"
#include "engine/core/Config.hpp"
#include "engine/ecs/components/Components.hpp"
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

  int32_t minX = static_cast<int32_t>(std::floor(minPoint.x));
  int32_t maxX = static_cast<int32_t>(std::floor(maxPoint.x));
  int32_t minY = static_cast<int32_t>(std::floor(minPoint.y));
  int32_t maxY = static_cast<int32_t>(std::floor(maxPoint.y));
  int32_t minZ = static_cast<int32_t>(std::floor(minPoint.z));
  int32_t maxZ = static_cast<int32_t>(std::floor(maxPoint.z));

  const int32_t chunkSize = m_config.chunkSize;
  const int32_t worldHeight = m_config.worldHeight;

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
        // Missing chunks behave like air so streaming boundaries do not create
        // invisible walls or floors at the player's current height.
        if (!cachedChunk) continue;

        if (m_world.isSolidInChunk(x, y, z, *cachedChunk)) return true;
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
  int32_t y = std::clamp(startY, 0, m_config.worldHeight - 1);
  const int32_t cx = floorToChunk(x, m_config.chunkSize);
  const int32_t cz = floorToChunk(z, m_config.chunkSize);
  const Chunk* chunk = m_world.getChunk(cx, cz);
  if (!chunk) return -1;

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
