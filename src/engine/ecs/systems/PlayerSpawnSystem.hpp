#pragma once

#include "engine/ecs/SystemManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/components/Components.hpp"
#include "engine/core/Config.hpp"
#include "world/World.hpp"

namespace voxel {

class Game;
class PlayerControllerSystem;

/// Handles the one-time operation of placing the player on the surface when
/// a new world starts.  Waits until the centre chunk has voxel data, scans a
/// 3x3 column grid for the highest ground level, and positions the player a
/// safe distance above it.
class PlayerSpawnSystem final : public System<Game> {
public:
  PlayerSpawnSystem(
    ComponentStore<cmp::Transform>& transforms,
    ComponentStore<cmp::RigidBody>& bodies,
    World& world,
    const GameConfig& config,
    bool& spawnedToSurface,
    bool& cameraDirty,
    PlayerControllerSystem* playerController);

  [[nodiscard]] auto name() const -> const std::string& override;
  [[nodiscard]] auto stage() const -> SystemStage override;
  void update(Game& state, float dt) override;

private:
  ComponentStore<cmp::Transform>& m_transforms;
  ComponentStore<cmp::RigidBody>& m_bodies;
  World& m_world;
  const GameConfig& m_config;
  bool& m_spawnedToSurface;
  bool& m_cameraDirty;
  PlayerControllerSystem* m_playerController;
};

} // namespace voxel
