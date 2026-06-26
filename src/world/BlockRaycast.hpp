#pragma once

#include "world/World.hpp"
#include "world/BlockIds.hpp"
#include "world/terrain/TerrainRaycast.hpp"
#include "world/terrain/TerrainCollision.hpp"
#include "world/ChunkCoords.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <limits>
#include <algorithm>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

struct BlockRaycastHit {
  bool hit = false;
  glm::ivec3 block{0};
  glm::ivec3 previous{0};
  uint8_t blockId = 0;
  float distance = 0.0f;
};

inline auto raycastFirstBlock(
    const World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance) -> BlockRaycastHit
{
  BlockRaycastHit blockResult;
  if (maxDistance <= 0.0f) return blockResult;

  const float dirLen2 = glm::dot(direction, direction);
  if (dirLen2 <= 0.0f) return blockResult;

  const glm::vec3 dir = glm::normalize(direction);
  glm::ivec3 cell(
      static_cast<int32_t>(std::floor(origin.x)),
      static_cast<int32_t>(std::floor(origin.y)),
      static_cast<int32_t>(std::floor(origin.z)));

  auto tryHitCell = [&](const glm::ivec3& candidate, const glm::ivec3& previous, float distance) -> bool {
    const uint8_t blockId = world.getBlockIdAt(candidate.x, candidate.y, candidate.z);
    if (blockId == 0) return false;

    // If the chunk has a terrain mesh, ignore natural blocks
    const int32_t cx = floorToChunk(candidate.x, world.config().chunkSize);
    const int32_t cz = floorToChunk(candidate.z, world.config().chunkSize);
    const Chunk* chunk = world.getChunk(cx, cz);
    if (chunk && chunk->terrainCollision && !chunk->terrainCollision->empty()) {
      bool isNatural = (blockId == BlockId::GRASS ||
                        blockId == BlockId::DIRT ||
                        blockId == BlockId::STONE ||
                        blockId == BlockId::SAND ||
                        blockId == BlockId::BEDROCK ||
                        blockId == BlockId::MOSSY_STONE);
      if (isNatural) return false;
    }

    blockResult.hit = true;
    blockResult.block = candidate;
    blockResult.previous = previous;
    blockResult.blockId = blockId;
    blockResult.distance = distance;
    return true;
  };

  // Perform block raycast
  bool blockHit = false;
  if (tryHitCell(cell, cell, 0.0f)) {
    blockHit = true;
  } else {
    const glm::ivec3 step(
        dir.x > 0.0f ? 1 : (dir.x < 0.0f ? -1 : 0),
        dir.y > 0.0f ? 1 : (dir.y < 0.0f ? -1 : 0),
        dir.z > 0.0f ? 1 : (dir.z < 0.0f ? -1 : 0));

    const float inf = std::numeric_limits<float>::infinity();
    auto initialTMax = [](float originCoord, int32_t cellCoord, float axisDir) -> float {
      if (axisDir == 0.0f) return std::numeric_limits<float>::infinity();
      const float boundary = static_cast<float>(cellCoord + (axisDir > 0.0f ? 1 : 0));
      return (boundary - originCoord) / axisDir;
    };

    glm::vec3 tMax(
        initialTMax(origin.x, cell.x, dir.x),
        initialTMax(origin.y, cell.y, dir.y),
        initialTMax(origin.z, cell.z, dir.z));
    glm::vec3 tDelta(
        dir.x == 0.0f ? inf : std::abs(1.0f / dir.x),
        dir.y == 0.0f ? inf : std::abs(1.0f / dir.y),
        dir.z == 0.0f ? inf : std::abs(1.0f / dir.z));

    float distance = 0.0f;
    while (distance <= maxDistance) {
      const glm::ivec3 previous = cell;

      if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
        cell.x += step.x;
        distance = tMax.x;
        tMax.x += tDelta.x;
      } else if (tMax.y <= tMax.z) {
        cell.y += step.y;
        distance = tMax.y;
        tMax.y += tDelta.y;
      } else {
        cell.z += step.z;
        distance = tMax.z;
        tMax.z += tDelta.z;
      }

      if (distance > maxDistance) break;
      if (tryHitCell(cell, previous, distance)) {
        blockHit = true;
        break;
      }
    }
  }

  // Perform smooth terrain raycast
  TerrainRaycastHit terrainHit = raycastTerrain(world, origin, direction, maxDistance);

  // Compare hits and return the closest one
  if (blockHit && terrainHit.hit) {
    if (terrainHit.distance < blockResult.distance) {
      blockHit = false; // terrain is closer
    }
  }

  if (terrainHit.hit && !blockHit) {
    BlockRaycastHit terrainResult;
    terrainResult.hit = true;
    terrainResult.distance = terrainHit.distance;

    // Move slightly along the normal into the terrain to find the hit block cell
    glm::vec3 insidePoint = terrainHit.position - terrainHit.normal * 0.05f;
    terrainResult.block = glm::ivec3(
        static_cast<int32_t>(std::floor(insidePoint.x)),
        static_cast<int32_t>(std::floor(insidePoint.y)),
        static_cast<int32_t>(std::floor(insidePoint.z)));

    // Move slightly along the normal away from the terrain to find the adjacent cell
    glm::vec3 outsidePoint = terrainHit.position + terrainHit.normal * 0.05f;
    terrainResult.previous = glm::ivec3(
        static_cast<int32_t>(std::floor(outsidePoint.x)),
        static_cast<int32_t>(std::floor(outsidePoint.y)),
        static_cast<int32_t>(std::floor(outsidePoint.z)));

    uint8_t bid = world.getBlockIdAt(terrainResult.block.x, terrainResult.block.y, terrainResult.block.z);
    terrainResult.blockId = (bid != 0) ? bid : BlockId::GRASS;
    return terrainResult;
  }

  return blockResult;
}

} // namespace voxel
