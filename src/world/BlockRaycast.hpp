#pragma once

#include "world/World.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <limits>

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
  BlockRaycastHit result;
  if (maxDistance <= 0.0f) return result;

  const float dirLen2 = glm::dot(direction, direction);
  if (dirLen2 <= 0.0f) return result;

  const glm::vec3 dir = glm::normalize(direction);
  glm::ivec3 cell(
      static_cast<int32_t>(std::floor(origin.x)),
      static_cast<int32_t>(std::floor(origin.y)),
      static_cast<int32_t>(std::floor(origin.z)));

  auto tryHitCell = [&](const glm::ivec3& candidate, const glm::ivec3& previous, float distance) -> bool {
    const uint8_t blockId = world.getBlockIdAt(candidate.x, candidate.y, candidate.z);
    if (blockId == 0) return false;
    result.hit = true;
    result.block = candidate;
    result.previous = previous;
    result.blockId = blockId;
    result.distance = distance;
    return true;
  };

  if (tryHitCell(cell, cell, 0.0f)) return result;

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
    if (tryHitCell(cell, previous, distance)) return result;
  }

  return result;
}

} // namespace voxel
