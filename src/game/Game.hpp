#pragma once

#include <GLFW/glfw3.h>
#include <random>
#include <memory>
#include <string>
#include <thread>

#include "engine/core/Config.hpp"
#include "engine/core/GameLoop.hpp"
#include "engine/core/InputState.hpp"
#include "engine/core/GameState.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/ecs/EntityManager.hpp"
#include "engine/ecs/ComponentStore.hpp"
#include "engine/ecs/SystemManager.hpp"
#include "engine/ecs/components/Components.hpp"
#include "engine/ecs/components/AudioEmitter.hpp"
#include "engine/ecs/systems/PlayerControllerSystem.hpp"
#include "engine/ecs/systems/PlayerSpawnSystem.hpp"
#include "engine/audio/AudioEngine.hpp"
#include "engine/threading/WorkerThreadPool.hpp"
#include "world/generation/WorldGenPipeline.hpp"
#include "world/BlockRegistry.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/render/CameraView.hpp"
#include "world/daynight/DayNightCycle.hpp"
#include "ui/UIManager.hpp"
#include "game/GameSession.hpp"
#include "game/WorldController.hpp"
#include "game/BlockInteractionAudio.hpp"

namespace voxel {

class Game {
public:
  struct Options {
    GameState initialState = GameState::MainMenu;
    GameMode initialGameMode = GameMode::Survival;
    std::string saveSlotId = "default";
    std::string saveDir = "./saves";
  };

  Game(GLFWwindow* window, const GameConfig& config, Options options);
  ~Game();

  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  void run();
  void update(float dt);
  void render(float alpha, float timeSeconds);

  auto input() -> InputState& { return m_input; }
  auto ui() -> UIManager& { return *m_ui; }
  auto session() -> GameSession& { return m_session; }
  auto world() -> World& { return m_worldController->world(); }
  auto worldController() -> WorldController& { return *m_worldController; }
  auto renderer() -> Renderer& { return *m_renderer; }
  auto camera() -> CameraView& { return m_camera; }
  auto blockRegistry() -> BlockRegistry& { return m_blocks; }
  auto audioEngine() -> audio::AudioEngine& { return m_audioEngine; }
  auto audioRegistry() -> audio::AudioRegistry& { return m_audioRegistry; }

  [[nodiscard]] auto playerEntityId() const -> int32_t { return m_playerEntityId; }
  void requestStop() { m_running = false; }

private:
  void initECS();
  void initSystems();
  void syncPlayerWithCamera();
  void createPlayer();
  [[nodiscard]] auto playerIndex() const -> int32_t;
  void startWorld(GameMode mode, const std::string& slotId, bool startFresh);
  void updateCamera();

  GLFWwindow* m_window;
  GameConfig m_config;
  GameSession m_session;
  bool m_running = true;

  InputState m_input;
  BlockRegistry m_blocks;
  std::unique_ptr<SharedPool> m_pool;
  std::unique_ptr<WorldController> m_worldController;
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<UIManager> m_ui;
  CameraView m_camera;

  audio::AudioEngine m_audioEngine;
  audio::AudioRegistry m_audioRegistry;
  std::unique_ptr<BlockInteractionAudio> m_blockAudio;

  // Threading
  std::unique_ptr<WorkerThreadPool> m_genPool;
  std::unique_ptr<WorkerThreadPool> m_meshPool;
  std::unique_ptr<WorkerThreadPool> m_ioPool;

  WorldGenPipeline m_worldGenPipeline;
  std::mt19937 m_worldSeedRng;
  daynight::DayNightCycle m_dayNightCycle;
  std::string m_saveDir;

  // ECS
  EntityManager m_entityManager{1 << 12};
  ComponentStore<cmp::Transform> m_transforms;
  ComponentStore<cmp::RigidBody> m_bodies;
  ComponentStore<cmp::Player> m_players;
  ComponentStore<cmp::Health> m_health;
  ComponentStore<cmp::MobStats> m_mobStats;
  ComponentStore<cmp::AudioEmitter> m_emitters;
  TagStore m_hostileTags;
  TagStore m_friendlyTags;

  SystemManager<Game> m_systems;
  PlayerControllerSystem* m_playerController = nullptr;
  bool m_spawnedToSurface = false;
  bool m_cameraDirty = true;
  int32_t m_playerEntityId = 0;
};

} // namespace voxel
