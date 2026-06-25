#include "EntityCollisions.hpp"
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
      int32_t cz = worldToChunk(static_cast<float>(z), chunkSize);
      for (int32_t x = minX; x <= maxX; ++x) {
        int32_t cx = worldToChunk(static_cast<float>(x), chunkSize);

        if (cx != cachedCX || cz != cachedCZ) {
          cachedChunk = m_world.getChunk(cx, cz);
          cachedCX = cx;
          cachedCZ = cz;
        }
        // Treat unloaded chunks as solid
        if (!cachedChunk) return true;

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
  int32_t x = static_cast<int32_t>(std::floor(worldX));
  int32_t z = static_cast<int32_t>(std::floor(worldZ));
  int32_t y = std::clamp(startY, 0, m_config.worldHeight - 1);

  for (; y >= 0; --y) {
    if (m_world.isSolid(x, y, z)) return y;
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

// ---------------------------------------------------------------------------
// Full per-frame movement resolution with sub-step collision
// ---------------------------------------------------------------------------
void EntityCollisions::resolveMovement(
    float dx, float dy, float dz,
    cmp::Transform& transform,
    cmp::RigidBody& body) const
{
  // Sub-step if any axis moves more than 0.5 blocks per step
  float maxDelta = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
  int32_t steps = std::max(1, static_cast<int32_t>(std::ceil(maxDelta / 0.5f)));
  float invSteps = 1.0f / static_cast<float>(steps);

  static constexpr float kStepHeight = 0.52f;

  for (int32_t s = 0; s < steps; ++s) {
    float subDx = dx * invSteps;
    float subDy = dy * invSteps;
    float subDz = dz * invSteps;

    glm::vec3 stepPos = transform.position;

    // ----- Y axis (gravity + landing) -----------------------------------
    glm::vec3 yStep = stepPos + glm::vec3(0.0f, subDy, 0.0f);
    if (!collidesAt(yStep, body)) {
      stepPos = yStep;
      body.onGround = 0;
    } else if (subDy <= 0.0f) {
      // Potential landing — scan downward from feet
      int32_t groundY = groundHeightAt(
          stepPos.x, stepPos.z,
          std::max(0, static_cast<int32_t>(
              std::floor(stepPos.y + body.aabbMin.y))));
      if (groundY >= 0) {
        float feetY   = stepPos.y + body.aabbMin.y;
        float surfaceY = static_cast<float>(groundY + 1);
        float distToGround = feetY - surfaceY;
        if (distToGround <= 1.5f) {
          // Ground is close — snap to it
          stepPos.y = surfaceY;
          body.onGround = 1;
          int32_t safety = 64;
          while (collidesAt(stepPos, body) && --safety > 0) {
            stepPos.y += 0.05f;
          }
          body.velocity.y = 0.0f;
          subDy = 0.0f;
        } else {
          // Ground far below — side collision at a hole edge
          stepPos = yStep;
          body.onGround = 0;
        }
      } else {
        // No ground anywhere — definitely a side collision
        stepPos = yStep;
        body.onGround = 0;
      }
    } else {
      // Hit ceiling
      body.velocity.y = 0.0f;
      subDy = 0.0f;
    }

    // ----- X axis (with step-assist) ------------------------------------
    glm::vec3 xStep = stepPos + glm::vec3(subDx, 0.0f, 0.0f);
    if (!collidesAt(xStep, body)) {
      stepPos.x = xStep.x;
    } else {
      if (body.onGround && subDx != 0.0f) {
        glm::vec3 raised = stepPos + glm::vec3(subDx, kStepHeight, 0.0f);
        if (!collidesAt(raised, body)) {
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
    if (!collidesAt(zStep, body)) {
      stepPos.z = zStep.z;
    } else {
      if (body.onGround && subDz != 0.0f) {
        glm::vec3 raised = stepPos + glm::vec3(0.0f, kStepHeight, subDz);
        if (!collidesAt(raised, body)) {
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

} // namespace voxel
