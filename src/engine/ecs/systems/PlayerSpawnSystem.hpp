#pragma once

#include "engine/ecs/SystemManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/components/Components.hpp"
#include "engine/core/TickContext.hpp"
#include "engine/core/Config.hpp"
#include "engine/collision/EntityCollisions.hpp"
#include "world/generation/WorldGenPipeline.hpp"

namespace terrain {

/// Handles the one-time operation of placing the player on the surface when
/// a new world starts.  Waits until the centre chunk has terrain data, scans a
/// 3x3 column grid for the highest ground level, and positions the player a
/// safe distance above it.
class PlayerSpawnSystem final : public System {
public:
  PlayerSpawnSystem(
    ComponentStore<cmp::Transform>& transforms,
    ComponentStore<cmp::RigidBody>& bodies,
    World& world,
    const WorldGenPipeline& pipeline,
    const GameConfig& config,
    bool& spawnedToSurface,
    bool& cameraDirty,
    EntityCollisions* collisions);

  [[nodiscard]] auto name() const -> const std::string& override;
  [[nodiscard]] auto stage() const -> SystemStage override;
  void update(TickContext& ctx) override;

private:
  ComponentStore<cmp::Transform>& m_transforms;
  ComponentStore<cmp::RigidBody>& m_bodies;
  World& m_world;
  const WorldGenPipeline& m_pipeline;
  const GameConfig& m_config;
  bool& m_spawnedToSurface;
  bool& m_cameraDirty;
  EntityCollisions* m_collisions;
};

} // namespace terrain
