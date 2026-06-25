#include "game/Game.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/workers/mesher/GreedyMesher.hpp"
#include "world/blocks/VanillaBlockFactory.hpp"
#include "content/flora/DefaultFlora.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <thread>

namespace voxel {

namespace {

/// Concrete IChunkWorker that dispatches generation and meshing
/// jobs to dedicated thread pools.
class ChunkWorkerImpl final : public IChunkWorker {
public:
  ChunkWorkerImpl(WorkerThreadPool& genPool, WorkerThreadPool& meshPool,
                  SharedPool& pool, WorldGenPipeline& pipeline,
                  const GameConfig& config, WorldController& controller,
                  BlockRegistry& blocks)
    : m_genPool(genPool), m_meshPool(meshPool), m_pool(pool),
      m_pipeline(pipeline), m_config(config),
      m_controller(controller), m_blocks(blocks) {}

  void generate(int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t seed) override {
    m_genPool.submitAndForget([this, slotIndex, chunkX, chunkZ, seed]() {
      auto slot = m_pool.view(slotIndex);
      m_pipeline.generate(slot.voxels, chunkX, chunkZ,
        m_config.chunkSize, m_config.worldHeight, m_config.chunkSize, seed);
      *slot.status = static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
      m_controller.onGenCompleted(slotIndex);
    });
  }

  void mesh(int32_t slotIndex) override {
    m_meshPool.submitAndForget([this, slotIndex]() {
      auto slot = m_pool.view(slotIndex);
      mesher::MesherConfig mcfg;
      mcfg.sizeX = m_config.chunkSize;
      mcfg.sizeY = m_config.worldHeight;
      mcfg.sizeZ = m_config.chunkSize;
      mcfg.maxVertices = m_config.maxVertsPerChunk;
      mcfg.maxIndices = m_config.maxIndicesPerChunk;
      mcfg.strideFloats = m_config.vertexStrideFloats;

      // Compute output pointers into the persistently mapped GPU VBO/IBO.
      // The mesher writes directly to GPU memory — no CPU staging copy needed.
      intptr_t vboBase = reinterpret_cast<intptr_t>(m_vboPtr);
      intptr_t iboBase = reinterpret_cast<intptr_t>(m_iboPtr);
      size_t vboStride = static_cast<size_t>(m_config.maxVertsPerChunk)
                       * m_config.vertexStrideFloats * sizeof(float);
      size_t iboStride = static_cast<size_t>(m_config.maxIndicesPerChunk)
                       * sizeof(uint32_t);

      uint32_t vc = 0, ic = 0;
      bool hasTransparent = false;
      bool ok = mesher::greedyMesh(
          slot.voxels, slot.light, m_blocks, mcfg,
          reinterpret_cast<float*>(vboBase + slotIndex * vboStride),
          reinterpret_cast<uint32_t*>(iboBase + slotIndex * iboStride),
          vc, ic, &hasTransparent);
      *slot.vertexCount = static_cast<uint32_t>(vc);
      *slot.indexCount = ic;
      *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
      if (hasTransparent) {
        *slot.status |= 0x10000;
      }
      m_controller.onMeshCompleted(slotIndex);
      (void)ok;
    });
  }

  void setGpuTargets(float* vboPtr, size_t vboMaxBytes,
                     uint32_t* iboPtr, size_t iboMaxBytes) override {
    m_vboPtr = vboPtr;
    m_vboMaxBytes = vboMaxBytes;
    m_iboPtr = iboPtr;
    m_iboMaxBytes = iboMaxBytes;
  }

private:
  WorkerThreadPool& m_genPool;
  WorkerThreadPool& m_meshPool;
  SharedPool& m_pool;
  WorldGenPipeline& m_pipeline;
  const GameConfig& m_config;
  WorldController& m_controller;
  BlockRegistry& m_blocks;
  float* m_vboPtr = nullptr;
  size_t m_vboMaxBytes = 0;
  uint32_t* m_iboPtr = nullptr;
  size_t m_iboMaxBytes = 0;
};

static auto makeDims(const GameConfig& cfg) -> ChunkDimensions {
  return {cfg.chunkSize, cfg.worldHeight, cfg.chunkSize,
          cfg.maxVertsPerChunk, cfg.maxIndicesPerChunk, cfg.vertexStrideFloats};
}

} // anonymous namespace

Game::Game(GLFWwindow* window, const GameConfig& config, Options options)
  : m_window(window), m_config(config),
    m_session(config.renderDistance),
    m_blocks(4096),
    m_worldGenPipeline(config.worldSeed),
    m_transforms(m_entityManager.capacity()),
    m_bodies(m_entityManager.capacity()),
    m_players(m_entityManager.capacity()),
    m_health(m_entityManager.capacity()),
    m_mobStats(m_entityManager.capacity()),
    m_emitters(m_entityManager.capacity()),
    m_hostileTags(m_entityManager.capacity()),
    m_friendlyTags(m_entityManager.capacity())
{
  {
    VanillaBlockFactory factory;
    factory.registerAll(m_blocks);
  }

  // Flora system — registers additional blocks and provides metadata
  m_flora = flora::createDefaultFloraRegistry();
  m_flora->registerAllBlocks(m_blocks);

  // Thread pool (use hardware concurrency, min 1)
  int32_t threads = std::max(1u, std::thread::hardware_concurrency());
  int32_t halfThreads = std::max(1, threads / 2);
  m_genPool = std::make_unique<WorkerThreadPool>(halfThreads);
  m_meshPool = std::make_unique<WorkerThreadPool>(halfThreads);
  // Dedicated I/O pool for async chunk loading (1-2 threads; disk I/O is serial-bound)
  m_ioPool = std::make_unique<WorkerThreadPool>(std::max(1, threads / 4));

  int32_t poolCap = (m_config.renderDistance * 2 + 1) * (m_config.renderDistance * 2 + 1) + 8;
  m_pool = SharedPool::create(poolCap, makeDims(config));

  // Wire world worker and persistence
  m_worldController = std::make_unique<WorldController>(*m_pool, m_blocks, config);
  m_chunkWorker = std::make_unique<ChunkWorkerImpl>(
    *m_genPool, *m_meshPool, *m_pool, m_worldGenPipeline, m_config,
    *m_worldController, m_blocks);
  // Persistence is nullptr initially; attached by configureSaveWorld
  m_worldController->createWorld(*m_chunkWorker, nullptr);

  m_renderer = std::make_unique<Renderer>(window, m_blocks, config);

  // Wire GPU VBO/IBO targets to the chunk worker so meshers write directly
  // to persistently mapped GPU memory, bypassing CPU-side staging buffers.
  m_chunkWorker->setGpuTargets(
      m_renderer->vboPtr(), m_renderer->vboBytes(),
      m_renderer->iboPtr(), m_renderer->iboBytes());

  m_saveDir = options.saveDir;
  m_currentSaveSlug = options.saveSlotId.empty() ? std::string("default") : options.saveSlotId;
  m_worldController->configureSaveWorld(m_saveDir, m_currentSaveSlug, false, m_ioPool.get());

  // Initialize save system orchestrator
  m_saveOrchestrator = std::make_unique<SaveOrchestrator>(m_saveDir);

  // Load saved settings (apply to UI after it's constructed below)
  auto saved = m_saveOrchestrator->loadSettings();
  int32_t savedRd = std::clamp(saved.renderDistance, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
  m_config.renderDistance = savedRd;
  m_session.setRenderDistance(savedRd);

  m_audioRegistry.seedBuiltinSounds();
  m_blockAudio = std::make_unique<BlockInteractionAudio>(m_audioEngine, m_audioRegistry, m_blocks);

  m_ui = std::make_unique<UIManager>(window, UIManager::Callbacks{
    .onStartWorld = [this](GameMode mode, const std::string& slotId, bool startFresh) {
      m_ui->clearWorldError();
      startWorld(mode, slotId, startFresh);
    },
    .onQuitToTitle = [this]{
      m_saveOrchestrator->onWorldClosed(m_worldController->saveManager());
      m_session.returnToTitle();
    },
    .onResume = [this]{ glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); },
    .onQuit = [this]{ m_running = false; },
    .onRenderDistanceChanged = [this]{
      int32_t rd = m_ui->renderDistance();
      applyRenderDistance(rd);
    },
  });

  // Apply saved settings to the now-constructed UI
  m_ui->setRenderDistance(saved.renderDistance);
  m_ui->setShowFps(saved.showFps);

  if (options.initialState == GameState::MainMenu) {
    m_ui->showMainMenu();
    m_session.enterMainMenu();
  }

  initECS();
  initSystems();

  m_camera.position = glm::vec3(0.0f, 80.0f, 50.0f);
  float aspect = 1280.0f / 720.0f;
  m_camera.projectionMatrix = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 1000.0f);
  m_camera.forward = glm::vec3(0.0f, 0.0f, -1.0f);
  m_camera.right = glm::vec3(1.0f, 0.0f, 0.0f);
  m_camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
  m_cameraDirty = true;
  syncPlayerWithCamera();
}

Game::~Game() {
  // Persist runtime settings and flush chunk saves
  m_saveOrchestrator->saveSettings(m_config.renderDistance, m_ui->isFpsVisible());
  m_saveOrchestrator->onWorldClosed(m_worldController->saveManager());
}

void Game::applyRenderDistance(int32_t newRd) {
  // Clamp to valid range
  newRd = std::clamp(newRd, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
  if (newRd == m_config.renderDistance) return;

  // 1. Flush pending saves and detach persistence
  if (m_worldController->saveManager()) {
    m_saveOrchestrator->onWorldClosed(m_worldController->saveManager());
  }

  // 2. Clear systems (they hold references to World)
  m_systems.clear();
  m_playerController = nullptr;

  // 3. Destroy render-dependent objects in reverse creation order
  m_renderer.reset();
  m_chunkWorker.reset();
  m_worldController.reset();
  m_pool.reset();

  // 4. Compute pool capacity from the requested render distance.
  int32_t poolCap = (newRd * 2 + 1) * (newRd * 2 + 1) + 8;

  // 5. Update config and session (will revert if allocation fails).
  int32_t oldRd = m_config.renderDistance;
  m_config.renderDistance = newRd;
  m_session.setRenderDistance(newRd);

  // 6. Try to recreate pool, world, and renderer. If allocation fails,
  //    revert to the previous working render distance.
  try {
    m_pool = SharedPool::create(poolCap, makeDims(m_config));

    m_worldController = std::make_unique<WorldController>(*m_pool, m_blocks, m_config);
    m_chunkWorker = std::make_unique<ChunkWorkerImpl>(
      *m_genPool, *m_meshPool, *m_pool, m_worldGenPipeline, m_config,
      *m_worldController, m_blocks);
    m_worldController->createWorld(*m_chunkWorker, nullptr);

    m_renderer = std::make_unique<Renderer>(m_window, m_blocks, m_config);

    // Allocation succeeded — persist the new render distance.
    m_saveOrchestrator->settings().setInt("renderDistance", newRd);
    m_ui->setRenderDistance(newRd);
  } catch (const std::bad_alloc&) {
    // Allocation failed — revert to the old render distance and recreate.
    m_renderer.reset();
    m_worldController.reset();
    m_pool.reset();

    m_config.renderDistance = oldRd;
    m_session.setRenderDistance(oldRd);

    int32_t oldPoolCap = (oldRd * 2 + 1) * (oldRd * 2 + 1) + 8;
    m_pool = SharedPool::create(oldPoolCap, makeDims(m_config));
    m_worldController = std::make_unique<WorldController>(*m_pool, m_blocks, m_config);
    m_chunkWorker = std::make_unique<ChunkWorkerImpl>(
      *m_genPool, *m_meshPool, *m_pool, m_worldGenPipeline, m_config,
      *m_worldController, m_blocks);
    m_worldController->createWorld(*m_chunkWorker, nullptr);
    m_renderer = std::make_unique<Renderer>(m_window, m_blocks, m_config);

    // Restore UI to old value (setting didn't take effect).
    m_ui->setRenderDistance(oldRd);
    return;
  }

  // 7. Reconfigure save (re-attach persistence)
  m_worldController->configureSaveWorld(m_saveDir, m_currentSaveSlug, false, m_ioPool.get());
  if (auto* sm = m_worldController->saveManager()) {
    m_saveOrchestrator->finalizeWorldStart(*sm, m_currentSaveSlug, m_currentSaveSlug,
                                           m_config.worldSeed, m_session.gameMode());
  }

  // 8. Rebuild systems (they reference the new World)
  initSystems();

  // 10. Reset player state
  m_spawnedToSurface = false;
  m_cameraDirty = true;
  syncPlayerWithCamera();
  m_input.clearAll();
  m_input.pointerLocked = false;
}

void Game::initECS() { createPlayer(); }

void Game::createPlayer() {
  m_playerEntityId = m_entityManager.allocate();
  int32_t idx = EntityManager::indexOf(m_playerEntityId);
  m_transforms.add(idx);
  m_bodies.add(idx);
  m_players.add(idx);

  auto& transform = m_transforms.get(idx);
  auto& body = m_bodies.get(idx);
  auto& player = m_players.get(idx);
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

void Game::initSystems() {
  // --- Player controller ---
  auto controller = std::make_unique<PlayerControllerSystem>(
    m_window, m_input, m_transforms, m_bodies, m_players,
    m_worldController->world(), m_camera, m_config, *m_ui, m_session,
    m_cameraDirty);
  // Keep a non-owning pointer for direct access (e.g. pushPlayerOutOfBlocks).
  m_playerController = controller.get();
  m_systems.add(std::move(controller));

  // --- Player spawn (runs once when terrain is ready) ---
  m_systems.add(std::make_unique<PlayerSpawnSystem>(
    m_transforms, m_bodies, m_worldController->world(), m_config,
    m_spawnedToSurface, m_cameraDirty, &m_playerController->collisions()));
}

void Game::startWorld(GameMode mode, const std::string& slotId, bool startFresh) {
  // 1. Resolve slug and prepare world parameters via the orchestrator
  std::string displayName = slotId;
  std::string slug;
  uint32_t seed = m_config.worldSeed;

  if (startFresh) {
    auto result = m_saveOrchestrator->prepareNewWorld(slotId, mode, seed);
    if (!result.error.empty()) {
      m_ui->setWorldError(result.error);
      return;
    }
    slug = std::move(result.slug);
    m_config.worldSeed = seed;
    m_worldGenPipeline = WorldGenPipeline(seed);
  } else {
    auto result = m_saveOrchestrator->prepareLoadWorld(slotId);
    if (!result.error.empty()) {
      m_ui->setWorldError(result.error);
      return;
    }
    slug = std::move(result.slug);
    seed = m_config.worldSeed;
  }

  m_currentSaveSlug = slug;

  // 2. Configure the world (creates SaveManager, sets up chunk persistence)
  m_worldController->configureSaveWorld(m_saveDir, slug, startFresh, m_ioPool.get());

  // 3. Finalize metadata via the orchestrator
  if (auto* sm = m_worldController->saveManager()) {
    m_saveOrchestrator->finalizeWorldStart(*sm, displayName, slug, seed, mode);
  }

  // 4. Transition session state
  m_session.startSingleplayer(mode);
  m_dayNightCycle.setTime(daynight::kMiddayTimeSeconds);

  // 5. Reset player / camera
  m_spawnedToSurface = false;
  m_camera.position = glm::vec3(0.0f, 80.0f, 50.0f);
  m_cameraDirty = true;
  syncPlayerWithCamera();
  m_input.clearAll();
  m_input.pointerLocked = false;

  const int32_t idx = playerIndex();
  if (idx >= 0 && m_transforms.has(idx) && m_bodies.has(idx) && m_players.has(idx)) {
    auto& body = m_bodies.get(idx);
    auto& player = m_players.get(idx);
    body.velocity = glm::vec3(0.0f);
    body.onGround = 0;
    body.isFluid = 0;
    player.yaw = 0.0f;
    player.pitch = 0.0f;
    player.selectedHotbarSlot = 0;
  }

  // 6. Switch UI to in-game
  m_ui->clearUI();
  m_ui->setInventoryOpen(false);
  glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // 7. Refresh world list
  m_saveOrchestrator->refreshWorldList();
}

auto Game::playerIndex() const -> int32_t {
  if (m_playerEntityId == 0) return -1;
  return EntityManager::indexOf(m_playerEntityId);
}

void Game::syncPlayerWithCamera() {
  const int32_t idx = playerIndex();
  if (idx < 0) return;
  if (!m_transforms.has(idx) || !m_bodies.has(idx) || !m_players.has(idx)) return;

  auto& transform = m_transforms.get(idx);
  const auto& player = m_players.get(idx);
  transform.position = glm::vec3(m_camera.position.x, m_camera.position.y - player.eyeHeight, m_camera.position.z);
}


void Game::update(float dt) {
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::GeneratingWorld) {
    m_dayNightCycle.advance(dt);

    m_worldController->processGenJobs();
    m_worldController->processSavePending();
    m_worldController->world().update(m_camera.position);

    // Build tick context and run ECS systems
    TickContext ctx{
      .input = m_input,
      .world = m_worldController->world(),
      .camera = m_camera,
      .ui = *m_ui,
      .session = m_session,
      .playerEntityId = m_playerEntityId,
      .cameraDirty = m_cameraDirty,
      .dt = dt,
    };
    m_systems.update(ctx);

    if (m_session.state() == GameState::GeneratingWorld && m_worldController->world().isReady()) {
      m_session.markWorldReady();
    }
  } else if (m_session.state() == GameState::MainMenu) {
    m_ui->setWorldList(
      SaveOrchestrator::buildWorldEntries(m_saveOrchestrator->worldList()));
  }
}

void Game::render(float, float) {
  const float worldTimeSeconds = m_dayNightCycle.time();
  updateCamera();
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::Paused ||
      m_session.state() == GameState::GeneratingWorld) {
    float daylightFactor = m_dayNightCycle.daylight();
    float ambientIntensity = m_dayNightCycle.ambientIntensity();
    float timeOfDay = worldTimeSeconds / m_dayNightCycle.dayLength();
    m_renderer->render(m_worldController->world(), m_camera,
                       worldTimeSeconds, daylightFactor,
                       ambientIntensity, timeOfDay);
  } else {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void Game::updateCamera() {
  if (!m_cameraDirty) return;
  m_cameraDirty = false;

  m_camera.viewMatrix = glm::lookAt(m_camera.position, m_camera.position + m_camera.forward, m_camera.up);
  m_camera.viewProjectionMatrix = m_camera.projectionMatrix * m_camera.viewMatrix;
  m_camera.inverseViewProjectionMatrix = glm::inverse(m_camera.viewProjectionMatrix);
}

void Game::run() {
  GameLoop loop(60.0f,
    [this](float dt) { update(dt); },
    [this, &loop](float, float time) {
      // Clear frame state BEFORE polling events so that mouse delta accumulated
      // during glfwPollEvents survives until the next frame's update() call.
      // (update() runs before render() in the GameLoop, so clearing here ensures
      //  the delta from this frame's poll is consumed by update next frame.)
      m_input.clearFrameState();
      glfwPollEvents();
      if (glfwWindowShouldClose(m_window) || !m_running) { loop.stop(); return; }

      if (m_input.isPressed(InputState::KEY_ESCAPE)) {
        if (m_ui->state() == UIState::InGame) {
          m_session.pause();
          m_ui->showPauseMenu();
          glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (m_ui->state() == UIState::Paused) {
          m_session.resume();
          m_ui->clearUI();
          glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
      }

      m_ui->beginFrame();
      render(0.0f, m_dayNightCycle.time());
      m_ui->endFrame();

      glfwSwapBuffers(m_window);
    }
  );
  loop.run();
}

} // namespace voxel
