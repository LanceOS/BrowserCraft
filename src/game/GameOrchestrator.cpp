#include "game/GameOrchestrator.hpp"
#include "game/ChunkWorkerImpl.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "world/blocks/VanillaBlockFactory.hpp"
#include "content/flora/DefaultFlora.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <new>
#include <utility>
#include <thread>

namespace voxel {

namespace {

static auto makeDims(const GameConfig& cfg) -> ChunkDimensions {
  return {cfg.chunkSize, cfg.worldHeight, cfg.chunkSize,
          cfg.maxVertsPerChunk, cfg.maxIndicesPerChunk, cfg.vertexStrideFloats};
}

} // namespace

void GameOrchestrator::buildRuntimeStack(Game& game) {
  int32_t poolCap = (game.m_config.renderDistance * 2 + 1) * (game.m_config.renderDistance * 2 + 1) + 8;
  game.m_pool = SharedPool::create(poolCap, makeDims(game.m_config));
  game.m_meshAllocator = std::make_unique<ChunkMeshAllocator>(game.m_config, poolCap);

  game.m_worldController = std::make_unique<WorldController>(*game.m_pool, game.m_blocks, game.m_config);
  game.m_chunkWorker = std::make_unique<ChunkWorkerImpl>(
    *game.m_genPool, *game.m_meshPool, *game.m_pool, game.m_worldGenPipeline,
    game.m_config, *game.m_worldController, game.m_blocks, *game.m_meshAllocator);
  // Persistence is attached later by configureSaveWorld.
  game.m_worldController->createWorld(*game.m_chunkWorker, nullptr);

  game.m_renderer = std::make_unique<Renderer>(game.m_window, game.m_blocks, game.m_config,
                                               *game.m_meshAllocator);
}

void GameOrchestrator::initialize(Game& game, GLFWwindow* window, const Game::Options& options) {
  {
    VanillaBlockFactory factory;
    factory.registerAll(game.m_blocks);
  }

  // Flora system — registers additional blocks and provides metadata
  game.m_flora = flora::createDefaultFloraRegistry();
  game.m_flora->registerAllBlocks(game.m_blocks);

  // Thread pool (use hardware concurrency, min 1)
  int32_t threads = std::max(1u, std::thread::hardware_concurrency());
  int32_t halfThreads = std::max(1, threads / 2);
  game.m_genPool = std::make_unique<WorkerThreadPool>(halfThreads);
  game.m_meshPool = std::make_unique<WorkerThreadPool>(halfThreads);
  // Dedicated I/O pool for async chunk loading (1-2 threads; disk I/O is serial-bound)
  game.m_ioPool = std::make_unique<WorkerThreadPool>(std::max(1, threads / 4));

  game.m_saveDir = options.saveDir;
  game.m_currentSaveSlug = options.saveSlotId.empty() ? std::string("default") : options.saveSlotId;

  // Initialize save system orchestrator
  game.m_saveOrchestrator = std::make_unique<SaveOrchestrator>(game.m_saveDir);

  // Load saved settings before building the runtime stack so the chunk pool,
  // world, and renderer all agree on render distance from the first frame.
  auto saved = game.m_saveOrchestrator->loadSettings();
  int32_t savedRd = saved.renderDistance;
  game.m_config.renderDistance = savedRd;
  game.m_session.setRenderDistance(savedRd);

  buildRuntimeStack(game);
  game.m_worldController->configureSaveWorld(game.m_saveDir, game.m_currentSaveSlug, false, game.m_ioPool.get());

  game.m_audioRegistry.seedBuiltinSounds();
  game.m_blockAudio = std::make_unique<BlockInteractionAudio>(game.m_audioEngine, game.m_audioRegistry, game.m_blocks);

  game.m_ui = std::make_unique<UIManager>(window, UIManager::Callbacks{
    .onStartWorld = [&game](GameMode mode, const std::string& slotId, bool startFresh) {
      game.m_ui->clearWorldError();
      GameOrchestrator::startWorld(game, mode, slotId, startFresh);
    },
    .onQuitToTitle = [&game]{
      game.m_saveOrchestrator->onWorldClosed(game.m_worldController->saveManager());
      game.m_session.returnToTitle();
    },
    .onResume = [&game]{ glfwSetInputMode(game.m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); },
    .onQuit = [&game]{ game.m_running = false; },
    .onRenderDistanceChanged = [&game]{
      int32_t rd = game.m_ui->renderDistance();
      GameOrchestrator::applyRenderDistance(game, rd);
    },
  });

  // Apply saved settings to the now-constructed UI
  game.m_ui->setRenderDistance(savedRd);
  game.m_ui->setShowFps(saved.showFps);

  if (options.initialState == GameState::MainMenu) {
    game.m_ui->showMainMenu();
    game.m_session.enterMainMenu();
  }

  initECS(game);
  initSystems(game);

  game.m_camera.position = glm::vec3(0.0f, 80.0f, 50.0f);
  float aspect = 1280.0f / 720.0f;
  game.m_camera.projectionMatrix = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 1000.0f);
  game.m_camera.forward = glm::vec3(0.0f, 0.0f, -1.0f);
  game.m_camera.right = glm::vec3(1.0f, 0.0f, 0.0f);
  game.m_camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
  game.m_cameraDirty = true;
  syncPlayerWithCamera(game);
}

void GameOrchestrator::shutdown(Game& game) {
  // Persist runtime settings and flush chunk saves
  if (game.m_saveOrchestrator) {
    game.m_saveOrchestrator->saveSettings(game.m_config.renderDistance, game.m_ui ? game.m_ui->isFpsVisible() : false);
    if (game.m_worldController) {
      game.m_saveOrchestrator->onWorldClosed(game.m_worldController->saveManager());
    }
  }
}

void GameOrchestrator::applyRenderDistance(Game& game, int32_t newRd) {
  // Clamp to valid range
  newRd = std::clamp(newRd, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
  if (newRd == game.m_config.renderDistance) return;

  // 1. Flush pending saves and detach persistence
  if (game.m_worldController->saveManager()) {
    game.m_saveOrchestrator->onWorldClosed(game.m_worldController->saveManager());
  }

  // 2. Clear systems (they hold references to World)
  game.m_systems.clear();
  game.m_playerController = nullptr;

  // 3. Destroy render-dependent objects in reverse creation order
  game.m_renderer.reset();
  game.m_chunkWorker.reset();
  game.m_worldController.reset();
  game.m_pool.reset();

  // 4. Update config and session (will revert if allocation fails).
  int32_t oldRd = game.m_config.renderDistance;
  game.m_config.renderDistance = newRd;
  game.m_session.setRenderDistance(newRd);

  // 5. Try to recreate pool, world, and renderer. If allocation fails,
  //    revert to the previous working render distance.
  try {
    buildRuntimeStack(game);

    // Allocation succeeded — persist the new render distance.
    game.m_saveOrchestrator->settings().setInt("renderDistance", newRd);
    game.m_ui->setRenderDistance(newRd);
  } catch (const std::bad_alloc&) {
    // Allocation failed — revert to the old render distance and recreate.
    game.m_renderer.reset();
    game.m_chunkWorker.reset();
    game.m_worldController.reset();
    game.m_meshAllocator.reset();
    game.m_pool.reset();

    game.m_config.renderDistance = oldRd;
    game.m_session.setRenderDistance(oldRd);

    buildRuntimeStack(game);

    // Restore UI to old value (setting didn't take effect).
    game.m_ui->setRenderDistance(oldRd);
    return;
  }

  // 6. Reconfigure save (re-attach persistence)
  game.m_worldController->configureSaveWorld(game.m_saveDir, game.m_currentSaveSlug, false, game.m_ioPool.get());
  if (auto* sm = game.m_worldController->saveManager()) {
    game.m_saveOrchestrator->finalizeWorldStart(*sm, game.m_currentSaveSlug, game.m_currentSaveSlug,
                                           game.m_config.worldSeed, game.m_session.gameMode());
  }

  // 7. Rebuild systems (they reference the new World)
  initSystems(game);

  // 8. Reset player state
  game.m_spawnedToSurface = false;
  game.m_cameraDirty = true;
  syncPlayerWithCamera(game);
  game.m_input.clearAll();
  game.m_input.pointerLocked = false;
}

void GameOrchestrator::initECS(Game& game) { createPlayer(game); }

void GameOrchestrator::createPlayer(Game& game) {
  game.m_playerEntityId = game.m_entityManager.allocate();
  int32_t idx = EntityManager::indexOf(game.m_playerEntityId);
  game.m_transforms.add(idx);
  game.m_bodies.add(idx);
  game.m_players.add(idx);

  auto& transform = game.m_transforms.get(idx);
  auto& body = game.m_bodies.get(idx);
  auto& player = game.m_players.get(idx);
  transform.position = glm::vec3(0.0f);
  transform.yaw = 0.0f;
  transform.pitch = 0.0f;
  transform.scale = 1.0f;
  body.velocity = glm::vec3(0.0f);
  body.onGround = 1;
  player.yaw = 0.0f;
  player.pitch = 0.0f;
  player.eyeHeight = 1.62f;
  player.selectedHotbarSlot = 0;
}

void GameOrchestrator::initSystems(Game& game) {
  // --- Player controller ---
  auto controller = std::make_unique<PlayerControllerSystem>(
    game.m_window, game.m_input, game.m_transforms, game.m_bodies, game.m_players,
    game.m_worldController->world(), game.m_camera, game.m_config, *game.m_ui, game.m_session,
    game.m_cameraDirty);
  // Keep a non-owning pointer for direct access (e.g. pushPlayerOutOfBlocks).
  game.m_playerController = controller.get();
  game.m_systems.add(std::move(controller));

  // --- Player spawn (runs once when terrain is ready) ---
  game.m_systems.add(std::make_unique<PlayerSpawnSystem>(
    game.m_transforms, game.m_bodies, game.m_worldController->world(), game.m_config,
    game.m_spawnedToSurface, game.m_cameraDirty, &game.m_playerController->collisions()));
}

void GameOrchestrator::startWorld(Game& game, GameMode mode, const std::string& slotId, bool startFresh) {
  // 1. Resolve slug and prepare world parameters via the orchestrator
  std::string displayName = slotId;
  std::string slug;
  uint32_t seed = game.m_config.worldSeed;

  if (startFresh) {
    auto result = game.m_saveOrchestrator->prepareNewWorld(slotId, mode, seed);
    if (!result.error.empty()) {
      game.m_ui->setWorldError(result.error);
      return;
    }
    slug = std::move(result.slug);
    game.m_config.worldSeed = seed;
    game.m_worldGenPipeline = WorldGenPipeline(seed);
  } else {
    auto result = game.m_saveOrchestrator->prepareLoadWorld(slotId);
    if (!result.error.empty()) {
      game.m_ui->setWorldError(result.error);
      return;
    }
    slug = std::move(result.slug);
    seed = game.m_config.worldSeed;
  }

  game.m_currentSaveSlug = slug;

  // 2. Configure the world (creates SaveManager, sets up chunk persistence)
  game.m_worldController->configureSaveWorld(game.m_saveDir, slug, startFresh, game.m_ioPool.get());

  // 3. Finalize metadata via the orchestrator
  if (auto* sm = game.m_worldController->saveManager()) {
    game.m_saveOrchestrator->finalizeWorldStart(*sm, displayName, slug, seed, mode);
  }

  // 4. Transition session state
  game.m_session.startSingleplayer(mode);
  game.m_dayNightCycle.setTime(daynight::kMiddayTimeSeconds);

  // 5. Reset player / camera
  game.m_spawnedToSurface = false;
  game.m_camera.position = glm::vec3(0.0f, 80.0f, 50.0f);
  game.m_cameraDirty = true;
  syncPlayerWithCamera(game);
  game.m_input.clearAll();
  game.m_input.pointerLocked = false;

  const int32_t idx = playerIndex(game);
  if (idx >= 0 && game.m_transforms.has(idx) && game.m_bodies.has(idx) && game.m_players.has(idx)) {
    auto& body = game.m_bodies.get(idx);
    auto& player = game.m_players.get(idx);
    body.velocity = glm::vec3(0.0f);
    body.onGround = 0;
    body.isFluid = 0;
    player.yaw = 0.0f;
    player.pitch = 0.0f;
    player.selectedHotbarSlot = 0;
  }

  // 6. Switch UI to in-game
  game.m_ui->clearUI();
  game.m_ui->setInventoryOpen(false);
  glfwSetInputMode(game.m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // 7. Refresh world list
  game.m_saveOrchestrator->refreshWorldList();
}

auto GameOrchestrator::playerIndex(const Game& game) -> int32_t {
  if (game.m_playerEntityId == 0) return -1;
  return EntityManager::indexOf(game.m_playerEntityId);
}

void GameOrchestrator::syncPlayerWithCamera(Game& game) {
  const int32_t idx = playerIndex(game);
  if (idx < 0) return;
  if (!game.m_transforms.has(idx) || !game.m_bodies.has(idx) || !game.m_players.has(idx)) return;

  auto& transform = game.m_transforms.get(idx);
  const auto& player = game.m_players.get(idx);
  transform.position = glm::vec3(game.m_camera.position.x, game.m_camera.position.y - player.eyeHeight, game.m_camera.position.z);
}

void GameOrchestrator::update(Game& game, float dt) {
  if (game.m_session.state() == GameState::InGame ||
      game.m_session.state() == GameState::GeneratingWorld) {
    game.m_dayNightCycle.advance(dt);

    game.m_worldController->processGenJobs();
    game.m_worldController->processSavePending();
    game.m_worldController->world().update(game.m_camera.position);

    // Build tick context and run ECS systems
    TickContext ctx{
      .input = game.m_input,
      .world = game.m_worldController->world(),
      .camera = game.m_camera,
      .ui = *game.m_ui,
      .session = game.m_session,
      .playerEntityId = game.m_playerEntityId,
      .cameraDirty = game.m_cameraDirty,
      .dt = dt,
    };
    game.m_systems.update(ctx);

    if (game.m_session.state() == GameState::GeneratingWorld && game.m_worldController->world().isReady()) {
      game.m_session.markWorldReady();
    }
  } else if (game.m_session.state() == GameState::MainMenu) {
    game.m_ui->setWorldList(
      SaveOrchestrator::buildWorldEntries(game.m_saveOrchestrator->worldList()));
  }
}

void GameOrchestrator::render(Game& game, float /*alpha*/, float timeSeconds) {
  updateCamera(game);
  if (game.m_session.state() == GameState::InGame ||
      game.m_session.state() == GameState::Paused ||
      game.m_session.state() == GameState::GeneratingWorld) {
    float daylightFactor = game.m_dayNightCycle.daylight();
    float ambientIntensity = game.m_dayNightCycle.ambientIntensity();
    float timeOfDay = timeSeconds / game.m_dayNightCycle.dayLength();
    game.m_renderer->render(game.m_worldController->world(), game.m_camera,
                       timeSeconds, daylightFactor,
                       ambientIntensity, timeOfDay);
  } else {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void GameOrchestrator::updateCamera(Game& game) {
  if (!game.m_cameraDirty) return;
  game.m_cameraDirty = false;

  game.m_camera.viewMatrix = glm::lookAt(game.m_camera.position, game.m_camera.position + game.m_camera.forward, game.m_camera.up);
  game.m_camera.viewProjectionMatrix = game.m_camera.projectionMatrix * game.m_camera.viewMatrix;
  game.m_camera.inverseViewProjectionMatrix = glm::inverse(game.m_camera.viewProjectionMatrix);
}

void GameOrchestrator::run(Game& game) {
  GameLoop loop(60.0f,
    [&game](float dt) { GameOrchestrator::update(game, dt); },
    [&game, &loop](float, float time) {
      // Clear frame state BEFORE polling events so that mouse delta accumulated
      // during glfwPollEvents survives until the next frame's update() call.
      // (update() runs before render() in the GameLoop, so clearing here ensures
      //  the delta from this frame's poll is consumed by update next frame.)
      game.m_input.clearFrameState();
      glfwPollEvents();
      if (glfwWindowShouldClose(game.m_window) || !game.m_running) { loop.stop(); return; }

      if (game.m_input.isPressed(InputState::KEY_ESCAPE)) {
        if (game.m_ui->state() == UIState::InGame) {
          game.m_session.pause();
          game.m_ui->showPauseMenu();
          glfwSetInputMode(game.m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (game.m_ui->state() == UIState::Paused) {
          game.m_session.resume();
          game.m_ui->clearUI();
          glfwSetInputMode(game.m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
      }

      game.m_ui->beginFrame();
      GameOrchestrator::render(game, 0.0f, game.m_dayNightCycle.time());
      game.m_ui->endFrame();

      glfwSwapBuffers(game.m_window);
    }
  );
  loop.run();
}

} // namespace voxel
