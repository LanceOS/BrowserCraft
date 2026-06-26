#include "PlayerSpawnSystem.hpp"
#include "engine/core/TickContext.hpp"
#include "world/ChunkCoords.hpp"
#include "world/World.hpp"
#include "engine/ecs/EntityManager.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxel {

PlayerSpawnSystem::PlayerSpawnSystem(
    ComponentStore<cmp::Transform>& transforms,
    ComponentStore<cmp::RigidBody>& bodies,
    World& world,
    const GameConfig& config,
    bool& spawnedToSurface,
    bool& cameraDirty,
    EntityCollisions* collisions)
  : m_transforms(transforms)
  , m_bodies(bodies)
  , m_world(world)
  , m_config(config)
  , m_spawnedToSurface(spawnedToSurface)
  , m_cameraDirty(cameraDirty)
  , m_collisions(collisions)
{}

auto PlayerSpawnSystem::name() const -> const std::string& {
  static const std::string kName = "PlayerSpawn";
  return kName;
}

auto PlayerSpawnSystem::stage() const -> SystemStage {
  return SystemStage::PrePhysics;
}

void PlayerSpawnSystem::update(TickContext& ctx) {
  // Only attempt spawn once, and only when the terrain is loaded.
  if (m_spawnedToSurface) return;
  if (!m_world.hasTerrain()) return;

  // Resolve player entity from TickContext
  int32_t playerEntityId = ctx.playerEntityId;
  int32_t idx = EntityManager::indexOf(playerEntityId);
  if (idx < 0 || !m_transforms.has(idx) || !m_bodies.has(idx)) return;

  auto& transform = m_transforms.get(idx);
  auto& body = m_bodies.get(idx);

  // Scan a 3x3 column area around the player's XZ position and use the
  // highest ground found.  This avoids caves or overhangs at the player's
  // exact coordinate causing them to spawn underground.
  int32_t gx = static_cast<int32_t>(std::floor(transform.position.x));
  int32_t gz = static_cast<int32_t>(std::floor(transform.position.z));

  // Wait until the entire 3x3 search window has voxel data, otherwise we can
  // accidentally spawn against a temporary low column while a taller neighbor
  // is still loading.
  auto isReadyColumn = [&](int32_t worldX, int32_t worldZ) -> bool {
    int32_t cx = floorToChunk(worldX, m_config.chunkSize);
    int32_t cz = floorToChunk(worldZ, m_config.chunkSize);
    const Chunk* chunk = m_world.getChunk(cx, cz);
    return chunk && chunk->state >= ChunkState::VoxelsReady;
  };
  for (int32_t dz = -1; dz <= 1; ++dz) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      if (!isReadyColumn(gx + dx, gz + dz)) return;
    }
  }

  int32_t highestGroundY = -1;
  int32_t bestGroundX = gx;
  int32_t bestGroundZ = gz;
  float bestDist2 = std::numeric_limits<float>::max();

  for (int32_t dz = -1; dz <= 1; ++dz) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      int32_t sx = gx + dx;
      int32_t sz = gz + dz;
      for (int32_t y = m_config.worldHeight - 1; y >= 0; --y) {
        if (m_world.isSolid(sx, y, sz)) {
          const float centerX = static_cast<float>(sx) + 0.5f;
          const float centerZ = static_cast<float>(sz) + 0.5f;
          const float distX = centerX - transform.position.x;
          const float distZ = centerZ - transform.position.z;
          const float dist2 = distX * distX + distZ * distZ;
          if (y > highestGroundY ||
              (y == highestGroundY && dist2 < bestDist2)) {
            highestGroundY = y;
            bestGroundX = sx;
            bestGroundZ = sz;
            bestDist2 = dist2;
          }
          break;
        }
      }
    }
  }

  if (highestGroundY >= 0) {
    // Place the player a few blocks above the highest nearby solid block
    // so they always spawn in clear space, even when the surface is
    // uneven or there's a cave shaft at their exact coordinates.
    constexpr float kSpawnHeightOffset = 3.0f;
    transform.position.x = static_cast<float>(bestGroundX) + 0.5f;
    transform.position.y = static_cast<float>(highestGroundY) + kSpawnHeightOffset;
    transform.position.z = static_cast<float>(bestGroundZ) + 0.5f;

    // Verify the player is not colliding; push upward in small steps if
    // they somehow ended up inside geometry (e.g. partial blocks,
    // overhangs, or terrain that generates after the scan).
    if (m_collisions && idx >= 0) m_collisions->pushOutOfBlocks(transform.position, body);

    // Start with zero velocity — let gravity pull the player down to
    // the surface rather than marking them as grounded immediately.
    body.velocity.y = 0.0f;
    body.onGround = 0;
    m_spawnedToSurface = true;
    m_cameraDirty = true;
  } else {
    // No terrain found at this XZ — clamp to a reasonable height and
    // wait for neighbouring chunks to finish generating.
    transform.position.y = std::min(transform.position.y, 64.0f);
    body.velocity.y = 0.0f;
    body.onGround = 0;
  }
}

} // namespace voxel
