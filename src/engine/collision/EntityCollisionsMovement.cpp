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
#include <utility>

namespace voxel {
namespace {

struct MovementSolver {
  const EntityCollisions& collisions;
  World& world;
  const GameConfig& config;
  cmp::RigidBody& body;

  static constexpr float kStepHeight = 0.52f;
  const int32_t chunkSize;

  MovementSolver(const EntityCollisions& collisions,
                 World& world,
                 const GameConfig& config,
                 cmp::RigidBody& body)
    : collisions(collisions)
    , world(world)
    , config(config)
    , body(body)
    , chunkSize(config.chunkSize)
  {}

  auto getGroundHeightFloat(float worldX, float worldZ, float startY) -> float {
    const int32_t cx = floorToChunk(static_cast<int32_t>(std::floor(worldX)), chunkSize);
    const int32_t cz = floorToChunk(static_cast<int32_t>(std::floor(worldZ)), chunkSize);
    const Chunk* chunk = world.getChunk(cx, cz);
    if (!chunk) return -1.0f;

    // 1. Check smooth terrain mesh first
    if (chunk->terrainCollision && !chunk->terrainCollision->empty()) {
      glm::vec3 origin(worldX, startY + 1.0f, worldZ);
      glm::vec3 direction(0.0f, -1.0f, 0.0f);
      glm::vec3 hitPos(0.0f);
      glm::vec3 hitNormal(0.0f);
      float hitDist = 0.0f;
      float maxDist = startY + 2.0f;

      if (chunk->terrainCollision->raycast(origin, direction, maxDist, hitPos, hitNormal, hitDist)) {
        return hitPos.y;
      }
    }

    // 2. Fall back to block grid
    const int32_t x = static_cast<int32_t>(std::floor(worldX));
    const int32_t z = static_cast<int32_t>(std::floor(worldZ));
    int32_t y = std::clamp(static_cast<int32_t>(std::floor(startY)), 0, config.worldHeight - 1);
    for (; y >= 0; --y) {
      if (world.isSolidInChunk(x, y, z, *chunk)) {
        return static_cast<float>(y + 1);
      }
    }
    return -1.0f;
  }

  auto highestGroundInFootprintFloat(const glm::vec3& probePos, float startY) -> float {
    const int32_t minGX = static_cast<int32_t>(std::floor(probePos.x + body.aabbMin.x));
    const int32_t maxGX = static_cast<int32_t>(std::floor(probePos.x + body.aabbMax.x));
    const int32_t minGZ = static_cast<int32_t>(std::floor(probePos.z + body.aabbMin.z));
    const int32_t maxGZ = static_cast<int32_t>(std::floor(probePos.z + body.aabbMax.z));
    float highest = -1.0f;
    for (int32_t gz = minGZ; gz <= maxGZ; ++gz) {
      for (int32_t gx = minGX; gx <= maxGX; ++gx) {
        const float groundY = getGroundHeightFloat(
            static_cast<float>(gx) + 0.5f,
            static_cast<float>(gz) + 0.5f,
            startY);
        if (groundY > highest) highest = groundY;
      }
    }
    return highest;
  }

  void resolve(float dx, float dy, float dz, cmp::Transform& transform) {
    const float maxDelta = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
    const int32_t steps = std::max(1, static_cast<int32_t>(std::ceil(maxDelta / 0.5f)));
    const float invSteps = 1.0f / static_cast<float>(steps);

    for (int32_t s = 0; s < steps; ++s) {
      float subDx = dx * invSteps;
      float subDy = dy * invSteps;
      float subDz = dz * invSteps;

      glm::vec3 stepPos = transform.position;

      // ----- Y axis (gravity + landing) -----------------------------------
      glm::vec3 yStep = stepPos + glm::vec3(0.0f, subDy, 0.0f);
      if (!collisions.collidesAt(yStep, body)) {
        stepPos = yStep;
        body.onGround = 0;
      } else if (subDy <= 0.0f) {
        const glm::vec3 supportProbePos = stepPos + glm::vec3(subDx, 0.0f, subDz);
        const float feetY = yStep.y + body.aabbMin.y;
        
        // Find the highest ground height in our footprint
        const float surfaceY = highestGroundInFootprintFloat(supportProbePos, stepPos.y + body.aabbMin.y);
        
        if (surfaceY >= 0.0f && (feetY - surfaceY) <= 1.5f) {
          // Ground is close — snap to it
          stepPos.y = surfaceY - body.aabbMin.y;
          body.onGround = 1;
          int32_t safety = 64;
          while (collisions.collidesAt(stepPos, body) && --safety > 0) {
            stepPos.y += 0.05f;
          }
          body.velocity.y = 0.0f;
          subDy = 0.0f;
        } else {
          body.velocity.y = 0.0f;
          subDy = 0.0f;
        }
      } else {
        // Hit ceiling
        body.velocity.y = 0.0f;
        subDy = 0.0f;
      }

      // ----- X axis (with step-assist) ------------------------------------
      glm::vec3 xStep = stepPos + glm::vec3(subDx, 0.0f, 0.0f);
      if (!collisions.collidesAt(xStep, body)) {
        stepPos.x = xStep.x;
      } else {
        if (body.onGround && subDx != 0.0f) {
          glm::vec3 raised = stepPos + glm::vec3(subDx, kStepHeight, 0.0f);
          if (!collisions.collidesAt(raised, body)) {
            stepPos = raised;
          } else {
            body.velocity.x = 0.0f;
          }
        } else {
          body.velocity.x = 0.0f;
        }
      }

      // ----- Z axis (with step-assist) ------------------------------------
      glm::vec3 zStep = stepPos + glm::vec3(0.0f, 0.0f, subDz);
      if (!collisions.collidesAt(zStep, body)) {
        stepPos.z = zStep.z;
      } else {
        if (body.onGround && subDz != 0.0f) {
          glm::vec3 raised = stepPos + glm::vec3(0.0f, kStepHeight, subDz);
          if (!collisions.collidesAt(raised, body)) {
            stepPos = raised;
          } else {
            body.velocity.z = 0.0f;
          }
        } else {
          body.velocity.z = 0.0f;
        }
      }

      transform.position = stepPos;
    }
  }
};

} // namespace

void EntityCollisions::resolveMovement(
    float dx, float dy, float dz,
    cmp::Transform& transform,
    cmp::RigidBody& body) const
{
  MovementSolver solver{*this, m_world, m_config, body};
  solver.resolve(dx, dy, dz, transform);
}

} // namespace voxel
