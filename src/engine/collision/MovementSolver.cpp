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

bool MovementSolver::resolveCollisions(glm::vec3& position, glm::vec3& velocity, bool& outOnGround) {
  outOnGround = false;
  bool collided = false;

  // Resolve up to 4 iterations of collision contacts
  for (int iter = 0; iter < 4; ++iter) {
    std::vector<TerrainTriangle> candidates;
    const glm::vec3 minPoint = position + m_body.aabbMin;
    const glm::vec3 maxPoint = position + m_body.aabbMax;

    // Expand the search bounds slightly to catch all potentially intersecting triangles
    const glm::vec3 searchMin = minPoint - glm::vec3(0.1f);
    const glm::vec3 searchMax = maxPoint + glm::vec3(0.1f);

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

    if (candidates.empty()) {
      break;
    }

    float maxDepth = -1.0f;
    CollisionContact deepestContact;

    const glm::vec3 boxCenter = position + 0.5f * (m_body.aabbMin + m_body.aabbMax);
    const glm::vec3 boxHalfSize = 0.5f * (m_body.aabbMax - m_body.aabbMin);

    for (const auto& tri : candidates) {
      CollisionContact contact;
      if (collideAABBTriangle(boxCenter, boxHalfSize, tri.v0, tri.v1, tri.v2, contact)) {
        if (contact.depth > maxDepth) {
          maxDepth = contact.depth;
          deepestContact = contact;
        }
      }
    }

    if (maxDepth <= 0.0f) {
      break;
    }

    // Resolve the deepest collision by pushing the position out
    position += deepestContact.normal * deepestContact.depth;
    collided = true;

    // A contact is ground if the normal points mostly up (e.g. normal.y > 0.5f)
    if (deepestContact.normal.y > 0.5f) {
      outOnGround = true;
    }

    // Project velocity onto the contact plane to slide
    float velDotN = glm::dot(velocity, deepestContact.normal);
    if (velDotN < 0.0f) {
      velocity -= deepestContact.normal * velDotN;
    }
  }

  return collided;
}

void MovementSolver::resolve(float dx, float dy, float dz, cmp::Transform& transform) {
  glm::vec3 originalPos = transform.position;
  glm::vec3 position = originalPos;
  glm::vec3 velocity = m_body.velocity;
  bool originallyOnGround = m_body.onGround != 0;

  // 1. Apply movement
  position += glm::vec3(dx, dy, dz);

  // 2. Resolve collisions at the new position
  bool onGround = false;
  resolveCollisions(position, velocity, onGround);

  // 3. Step-assist check
  // If the player was on the ground, has horizontal movement, and got blocked:
  float horizontalMoveDistSq = dx * dx + dz * dz;
  if (originallyOnGround && horizontalMoveDistSq > 1e-6f) {
    float dxTravelled = position.x - originalPos.x;
    float dzTravelled = position.z - originalPos.z;
    float travelDistSq = dxTravelled * dxTravelled + dzTravelled * dzTravelled;
    
    float expectedDistSq = dx * dx + dz * dz;

    // If we travelled less than 90% of the expected horizontal distance, we might be blocked by a step
    if (travelDistSq < expectedDistSq * 0.9f) {
      glm::vec3 stepUpPos = originalPos;
      stepUpPos.y += kStepHeight; // Lift up

      // Move horizontally at the elevated height
      stepUpPos += glm::vec3(dx, 0.0f, dz);

      // Resolve collisions at this elevated position
      bool stepOnGround = false;
      glm::vec3 stepVelocity = velocity;
      resolveCollisions(stepUpPos, stepVelocity, stepOnGround);

      // Pull back down
      stepUpPos.y -= kStepHeight;
      resolveCollisions(stepUpPos, stepVelocity, stepOnGround);

      // If step-up was successful:
      // - We ended up higher than our original position
      // - We landed on a walkable surface
      // - We travelled further horizontally than the blocked path
      if (stepUpPos.y > position.y && stepOnGround) {
        float stepDx = stepUpPos.x - originalPos.x;
        float stepDz = stepUpPos.z - originalPos.z;
        float stepTravelDistSq = stepDx * stepDx + stepDz * stepDz;

        if (stepTravelDistSq > travelDistSq) {
          position = stepUpPos;
          velocity = stepVelocity;
          onGround = true;
        }
      }
    }
  }

  // 4. Update rigid body state
  m_body.onGround = onGround ? 1 : 0;
  m_body.velocity = velocity;

  transform.position = position;
}

} // namespace terrain::physics
