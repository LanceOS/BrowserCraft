#include "MovementSolver.hpp"
#include "EntityCollisions.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCoords.hpp"
#include "engine/core/Config.hpp"
#include "world/terrain/TerrainCollision.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <utility>

namespace terrain::physics {

MovementSolver::MovementSolver(const EntityCollisions& collisions,
                               World& world,
                               const GameConfig& config,
                               cmp::RigidBody& body)
  : m_collisions(collisions)
  , m_world(world)
  , m_config(config)
  , m_body(body)
  , m_chunkSize(config.chunkSize)
{}

auto MovementSolver::getGroundHeightFloat(float worldX, float worldZ, float startY) -> float {
  const int32_t cx = floorToChunk(static_cast<int32_t>(std::floor(worldX)), m_chunkSize);
  const int32_t cz = floorToChunk(static_cast<int32_t>(std::floor(worldZ)), m_chunkSize);
  const Chunk* chunk = m_world.getChunk(cx, cz);
  if (!chunk) return -1.0f;

  // Check smooth terrain mesh
  if (chunk->terrainCollision && !chunk->terrainCollision->empty()) {
    glm::vec3 origin(worldX, startY + 1.0f, worldZ);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    glm::vec3 hitPos(0.0f);
    glm::vec3 hitNormal(0.0f);
    float hitDist = 0.0f;
    float maxDist = 2.0f;

    if (chunk->terrainCollision->raycast(origin, direction, maxDist, hitPos, hitNormal, hitDist)) {
      return hitPos.y;
    }
  }
  return -1.0f;
}

auto MovementSolver::highestGroundInFootprintFloat(const glm::vec3& probePos, float startY) -> float {
  float highest = -1.0f;
  
  // Sample points: center and the 4 corners of the AABB footprint
  const float pts[5][2] = {
    {probePos.x, probePos.z},
    {probePos.x + m_body.aabbMin.x, probePos.z + m_body.aabbMin.z},
    {probePos.x + m_body.aabbMin.x, probePos.z + m_body.aabbMax.z},
    {probePos.x + m_body.aabbMax.x, probePos.z + m_body.aabbMin.z},
    {probePos.x + m_body.aabbMax.x, probePos.z + m_body.aabbMax.z},
  };
  
  for (int i = 0; i < 5; ++i) {
    const float groundY = getGroundHeightFloat(pts[i][0], pts[i][1], startY);
    if (groundY > highest) highest = groundY;
  }
  
  return highest;
}



void MovementSolver::resolve(float dx, float dy, float dz, cmp::Transform& transform) {
  // 1. Transform the player's bounding box into an ellipsoid radius (eRadius)
  // This allows us to sweep a sphere of radius 1 (a unit sphere) against the terrain.
  glm::vec3 eRadius = (m_body.aabbMax - m_body.aabbMin) * 0.5f;
  glm::vec3 centerOffset = m_body.aabbMin + eRadius;

  // 2. Convert origin and velocity into Ellipsoid Space (eSpace)
  // In eSpace, the player is a perfect sphere of radius 1.
  glm::vec3 eOrigin = (transform.position + centerOffset) / eRadius;
  glm::vec3 eVel = glm::vec3(dx, dy, dz) / eRadius;

  bool onGround = false;

  // 3. The Kasper Fauerby Sliding Loop
  // We allow up to 4 collisions ("bumps") per frame to smoothly slide up ramps and along walls.
  for (int bump = 0; bump < 4; ++bump) {
    float velLen = glm::length(eVel);
    if (velLen < 1e-6f) break; // Stop if we have no remaining velocity

    // Search bounds in world space
    glm::vec3 wOrigin = eOrigin * eRadius;
    glm::vec3 wVel = eVel * eRadius;
    
    glm::vec3 searchMin = glm::min(wOrigin, wOrigin + wVel) - eRadius - glm::vec3(0.1f);
    glm::vec3 searchMax = glm::max(wOrigin, wOrigin + wVel) + eRadius + glm::vec3(0.1f);

    std::vector<TerrainTriangle> candidates;
    int32_t minCX = floorToChunk(static_cast<int32_t>(std::floor(searchMin.x)), m_chunkSize);
    int32_t maxCX = floorToChunk(static_cast<int32_t>(std::floor(searchMax.x)), m_chunkSize);
    int32_t minCZ = floorToChunk(static_cast<int32_t>(std::floor(searchMin.z)), m_chunkSize);
    int32_t maxCZ = floorToChunk(static_cast<int32_t>(std::floor(searchMax.z)), m_chunkSize);

    for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
      for (int32_t cx = minCX; cx <= maxCX; ++cx) {
        const Chunk* chunk = m_world.getChunk(cx, cz);
        if (chunk && chunk->terrainCollision && !chunk->terrainCollision->empty()) {
          chunk->terrainCollision->getTrianglesIntersectingAABB(searchMin, searchMax, candidates);
        }
      }
    }

    SweepContact bestContact;
    bestContact.t = 1.0f;

    for (const auto& tri : candidates) {
      glm::vec3 p1 = tri.v0 / eRadius;
      glm::vec3 p2 = tri.v1 / eRadius;
      glm::vec3 p3 = tri.v2 / eRadius;
      sweepSphereTriangle(eOrigin, eVel, p1, p2, p3, bestContact);
    }

    // If no collision occurred during the sweep, we can safely move the full distance
    if (!bestContact.hit) {
      eOrigin += eVel;
      break;
    }

    // 5. Collision detected!
    float t = bestContact.t;
    
    // We want to stop slightly before the actual point of impact to prevent
    // floating-point inaccuracies from embedding us in the triangle.
    const float veryCloseDistance = 0.005f;
    
    // If we are moving far enough this frame, we safely move up to the point just before impact.
    if (t * velLen > veryCloseDistance) {
      float safeT = t - (veryCloseDistance / velLen);
      eOrigin += eVel * safeT;
    }
    // If we are already extremely close, we do not move eOrigin this frame,
    // we just project the velocity to slide along the surface on the next bump.

    // Check if the surface normal points upwards (to determine if we landed on the ground).
    // The normal must be transformed back to world space for an accurate Y-axis check.
    glm::vec3 wNormal = glm::normalize(bestContact.normal / eRadius);
    if (wNormal.y > 0.5f) {
      onGround = true;
    }

    // Clip the physics engine's persistent velocity against the collision normal.
    // This stops gravity from accumulating indefinitely while standing on the ground.
    float velDotN = glm::dot(m_body.velocity, wNormal);
    if (velDotN < 0.0f) {
      m_body.velocity -= wNormal * velDotN;
    }

    // 6. Calculate Sliding Velocity
    // We determine how much time/distance is remaining in this frame's sweep.
    float timeRemaining = 1.0f - t;
    eVel *= timeRemaining;
    
    // Project the remaining velocity onto the collision plane.
    // This allows the player to seamlessly glide along walls or slide up ramps.
    float distanceToPlane = glm::dot(eVel, bestContact.normal);
    eVel -= bestContact.normal * distanceToPlane;
  }

  // 7. Convert the final eSpace position back to World Space
  transform.position = (eOrigin * eRadius) - centerOffset;
  m_body.onGround = onGround ? 1 : 0;
}

} // namespace terrain::physics
