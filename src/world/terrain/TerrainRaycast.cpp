#include "TerrainRaycast.hpp"
#include "world/World.hpp"
#include "TerrainCollision.hpp"
#include "world/ChunkCoords.hpp"
#include <algorithm>
#include <limits>

namespace terrain {

// Declare the ray-AABB intersection helper defined in TerrainCollision.cpp
extern bool rayAABBIntersect(const glm::vec3& origin, const glm::vec3& dir,
                             const glm::vec3& boxMin, const glm::vec3& boxMax,
                             float& tMin, float& tMax);

auto raycastTerrain(
    const World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance) -> TerrainRaycastHit
{
  TerrainRaycastHit result;
  if (maxDistance <= 0.0f) return result;

  const float dirLen2 = glm::dot(direction, direction);
  if (dirLen2 <= 0.0f) return result;

  const glm::vec3 dir = glm::normalize(direction);

  // Find bounding box of the ray segment
  glm::vec3 rayEnd = origin + dir * maxDistance;
  float minX = std::min(origin.x, rayEnd.x);
  float maxX = std::max(origin.x, rayEnd.x);
  float minZ = std::min(origin.z, rayEnd.z);
  float maxZ = std::max(origin.z, rayEnd.z);

  const int32_t chunkSize = world.config().chunkSize;

  int32_t minCX = floorToChunk(static_cast<int32_t>(std::floor(minX)), chunkSize);
  int32_t maxCX = floorToChunk(static_cast<int32_t>(std::floor(maxX)), chunkSize);
  int32_t minCZ = floorToChunk(static_cast<int32_t>(std::floor(minZ)), chunkSize);
  int32_t maxCZ = floorToChunk(static_cast<int32_t>(std::floor(maxZ)), chunkSize);

  float closestDist = maxDistance;
  glm::vec3 closestPos(0.0f);
  glm::vec3 closestNormal(0.0f);
  bool hitAny = false;

  for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
    for (int32_t cx = minCX; cx <= maxCX; ++cx) {
      const Chunk* chunk = world.getChunk(cx, cz);
      if (!chunk || !chunk->terrainCollision || chunk->terrainCollision->empty()) {
        continue;
      }

      // Quick ray-AABB check for the chunk bounds
      glm::vec3 chunkMin(static_cast<float>(cx * chunkSize), 0.0f, static_cast<float>(cz * chunkSize));
      glm::vec3 chunkMax(static_cast<float>((cx + 1) * chunkSize), static_cast<float>(world.config().worldHeight), static_cast<float>((cz + 1) * chunkSize));
      
      float tMin = 0.0f, tMax = 0.0f;
      if (!rayAABBIntersect(origin, dir, chunkMin, chunkMax, tMin, tMax)) {
        continue;
      }
      if (tMin > closestDist) {
        continue;
      }

      glm::vec3 hitPos(0.0f);
      glm::vec3 hitNormal(0.0f);
      float hitDist = 0.0f;
      if (chunk->terrainCollision->raycast(origin, dir, closestDist, hitPos, hitNormal, hitDist)) {
        if (hitDist < closestDist) {
          closestDist = hitDist;
          closestPos = hitPos;
          closestNormal = hitNormal;
          hitAny = true;
        }
      }
    }
  }

  if (hitAny) {
    result.hit = true;
    result.position = closestPos;
    result.normal = closestNormal;
    result.distance = closestDist;
  }
  return result;
}

} // namespace terrain
