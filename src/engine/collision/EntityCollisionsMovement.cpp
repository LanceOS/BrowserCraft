#include "EntityCollisions.hpp"
#include "world/ChunkCoords.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"
#include "engine/core/Config.hpp"
#include "engine/ecs/components/Components.hpp"
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
  static constexpr int32_t kSentinel = int32_t(0x7FFFFFFF);
  static constexpr int32_t kGroundCacheCapacity = 16;

  struct GroundSample {
    int32_t x;
    int32_t z;
    int32_t startY;
    int32_t height;
    bool valid;
  };

  GroundSample groundCache[kGroundCacheCapacity]{};
  int32_t groundCacheCursor = 0;
  const int32_t chunkSize;
  const Chunk* supportChunk = nullptr;
  int32_t supportCX = kSentinel;
  int32_t supportCZ = kSentinel;

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

  auto getGroundHeightCached(float worldX, float worldZ, int32_t startY) -> int32_t {
    const int32_t x = static_cast<int32_t>(std::floor(worldX));
    const int32_t z = static_cast<int32_t>(std::floor(worldZ));
    const int32_t worldHeight = config.worldHeight;
    const int32_t clampedStartY = std::clamp(startY, 0, worldHeight - 1);

    for (int32_t i = 0; i < kGroundCacheCapacity; ++i) {
      const GroundSample& sample = groundCache[i];
      if (sample.valid && sample.x == x && sample.z == z &&
          sample.startY == clampedStartY) {
        return sample.height;
      }
    }

    int32_t y = clampedStartY;
    int32_t height = -1;
    const int32_t cx = floorToChunk(x, chunkSize);
    const int32_t cz = floorToChunk(z, chunkSize);
    const Chunk* chunk = world.getChunk(cx, cz);
    if (chunk) {
      for (; y >= 0; --y) {
        if (world.isSolidInChunk(x, y, z, *chunk)) {
          height = y;
          break;
        }
      }
    }

    const int32_t idx = groundCacheCursor++ % kGroundCacheCapacity;
    groundCache[idx] = {x, z, clampedStartY, height, true};
    return height;
  }

  auto isSolidAtY(int32_t worldX, int32_t worldZ, int32_t y) -> bool {
    const int32_t cx = floorToChunk(worldX, chunkSize);
    const int32_t cz = floorToChunk(worldZ, chunkSize);
    if (cx != supportCX || cz != supportCZ) {
      supportChunk = world.getChunk(cx, cz);
      supportCX = cx;
      supportCZ = cz;
    }
    if (!supportChunk) return false;
    return world.isSolidInChunk(worldX, y, worldZ, *supportChunk);
  }

  auto countSupportColumns(const glm::vec3& probePos, int32_t checkY)
      -> std::pair<int32_t, int32_t>
  {
    const int32_t minGX = static_cast<int32_t>(std::floor(probePos.x + body.aabbMin.x));
    const int32_t maxGX = static_cast<int32_t>(std::floor(probePos.x + body.aabbMax.x));
    const int32_t minGZ = static_cast<int32_t>(std::floor(probePos.z + body.aabbMin.z));
    const int32_t maxGZ = static_cast<int32_t>(std::floor(probePos.z + body.aabbMax.z));
    int32_t solidCols = 0;
    int32_t totalCols = 0;
    for (int32_t gz = minGZ; gz <= maxGZ; ++gz) {
      for (int32_t gx = minGX; gx <= maxGX; ++gx) {
        ++totalCols;
        if (isSolidAtY(gx, gz, checkY)) ++solidCols;
      }
    }
    return {solidCols, totalCols};
  }

  auto highestGroundInFootprint(const glm::vec3& probePos, int32_t startY) -> int32_t {
    const int32_t minGX = static_cast<int32_t>(std::floor(probePos.x + body.aabbMin.x));
    const int32_t maxGX = static_cast<int32_t>(std::floor(probePos.x + body.aabbMax.x));
    const int32_t minGZ = static_cast<int32_t>(std::floor(probePos.z + body.aabbMin.z));
    const int32_t maxGZ = static_cast<int32_t>(std::floor(probePos.z + body.aabbMax.z));
    int32_t highest = -1;
    for (int32_t gz = minGZ; gz <= maxGZ; ++gz) {
      for (int32_t gx = minGX; gx <= maxGX; ++gx) {
        const int32_t groundY = getGroundHeightCached(
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
      bool hasPartialSupportClearance = false;
      float partialSupportClearanceY = 0.0f;

      // ----- Y axis (gravity + landing) -----------------------------------
      glm::vec3 yStep = stepPos + glm::vec3(0.0f, subDy, 0.0f);
      if (!collisions.collidesAt(yStep, body)) {
        stepPos = yStep;
        body.onGround = 0;
      } else if (subDy <= 0.0f) {
        const glm::vec3 supportProbePos = stepPos + glm::vec3(subDx, 0.0f, subDz);
        const int32_t checkY =
            static_cast<int32_t>(std::floor(yStep.y + body.aabbMin.y));
        const auto [solidCols, totalCols] = countSupportColumns(supportProbePos, checkY);
        const bool fullySupported = totalCols > 0 && solidCols == totalCols;

        // Potential landing — scan downward from feet
        const int32_t groundY = getGroundHeightCached(
            supportProbePos.x,
            supportProbePos.z,
            std::max(0, checkY));
        if (fullySupported && groundY >= 0) {
          const float feetY = yStep.y + body.aabbMin.y;
          const float surfaceY = static_cast<float>(groundY + 1);
          const float distToGround = feetY - surfaceY;
          if (distToGround <= 1.5f) {
            // Ground is close — snap to it
            stepPos.y = surfaceY;
            body.onGround = 1;
            int32_t safety = 64;
            while (collisions.collidesAt(stepPos, body) && --safety > 0) {
              stepPos.y += 0.05f;
            }
            body.velocity.y = 0.0f;
            subDy = 0.0f;
          }
        }  // groundY >= 0

        if (!fullySupported) {
          // Remember the ledge top so X/Z can move off it even while falling
          // below that height; otherwise the old ledge becomes a side wall.
          const int32_t supportTopY = highestGroundInFootprint(
              supportProbePos,
              std::max(0, static_cast<int32_t>(
                  std::floor(stepPos.y + body.aabbMin.y))));
          if (supportTopY >= 0) {
            hasPartialSupportClearance = true;
            partialSupportClearanceY = static_cast<float>(supportTopY + 1);
          }
          stepPos = yStep;
          body.onGround = 0;
        } else if (groundY < 0 ||
                   (groundY >= 0 &&
                    (yStep.y + body.aabbMin.y - static_cast<float>(groundY + 1)) > 1.5f)) {
          body.velocity.y = 0.0f;
          body.onGround = 1;
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
        bool movedAcrossPartialSupport = false;
        if (hasPartialSupportClearance && subDy < 0.0f) {
          glm::vec3 clearanceStep = xStep;
          clearanceStep.y = partialSupportClearanceY;
          if (!collisions.collidesAt(clearanceStep, body)) {
            stepPos.x = xStep.x;
            movedAcrossPartialSupport = true;
          }
        }
        if (!movedAcrossPartialSupport) {
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
      }

      // ----- Z axis (with step-assist) ------------------------------------
      glm::vec3 zStep = stepPos + glm::vec3(0.0f, 0.0f, subDz);
      if (!collisions.collidesAt(zStep, body)) {
        stepPos.z = zStep.z;
      } else {
        bool movedAcrossPartialSupport = false;
        if (hasPartialSupportClearance && subDy < 0.0f) {
          glm::vec3 clearanceStep = zStep;
          clearanceStep.y = partialSupportClearanceY;
          if (!collisions.collidesAt(clearanceStep, body)) {
            stepPos.z = zStep.z;
            movedAcrossPartialSupport = true;
          }
        }
        if (!movedAcrossPartialSupport) {
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
