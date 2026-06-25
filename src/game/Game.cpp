#include "game/Game.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/workers/mesher/GreedyMesher.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>
#include "engine/assets/AssetManager.hpp"

namespace voxel {

namespace {
} // namespace

static auto makeDims(const GameConfig& cfg) -> ChunkDimensions {
  return {cfg.chunkSize, cfg.worldHeight, cfg.chunkSize,
          cfg.maxVertsPerChunk, cfg.maxIndicesPerChunk, cfg.vertexStrideFloats};
}

// ---- Block registration ----
static void registerVanillaBlocks(BlockRegistry& blocks) {
  AssetManager::get().loadAssets();
  
  for (const auto& [id, def] : AssetManager::get().getBlockDefs()) {
    if (id == 0) continue; // Skip air
    
    BlockDefinition bd;
    bd.id = def.id;
    bd.name = def.name;
    bd.textures.top = def.tex_top;
    bd.textures.bottom = def.tex_bottom;
    bd.textures.side = def.tex_side;
    bd.material.opaque = def.is_opaque;
    if (!def.is_opaque) {
        bd.material.transparent = true;
    }
    blocks.register_(std::move(bd));
  }
}

Game::Game(GLFWwindow* window, const GameConfig& config, Options options)
  : m_window(window), m_config(config),
    m_session(config.renderDistance),
    m_blocks(4096),
    m_worldGenPipeline(config.worldSeed),
    m_worldSeedRng(std::random_device{}()),
    m_transforms(m_entityManager.capacity()),
    m_bodies(m_entityManager.capacity()),
    m_players(m_entityManager.capacity()),
    m_health(m_entityManager.capacity()),
    m_mobStats(m_entityManager.capacity()),
    m_emitters(m_entityManager.capacity()),
    m_hostileTags(m_entityManager.capacity()),
    m_friendlyTags(m_entityManager.capacity())
{
  registerVanillaBlocks(m_blocks);

  // Thread pool (use hardware concurrency, min 1)
  int32_t threads = std::max(1u, std::thread::hardware_concurrency());
  int32_t halfThreads = std::max(1, threads / 2);
  m_genPool = std::make_unique<WorkerThreadPool>(halfThreads);
  m_meshPool = std::make_unique<WorkerThreadPool>(halfThreads);
  // Dedicated I/O pool for async chunk loading (1-2 threads; disk I/O is serial-bound)
  m_ioPool = std::make_unique<WorkerThreadPool>(std::max(1, threads / 4));

  int32_t poolCap = (MAX_RENDER_DISTANCE * 2 + 1) * (MAX_RENDER_DISTANCE * 2 + 1) + 8;
  m_pool = SharedPool::create(poolCap, makeDims(config));

  // Wire world gen callback through thread pool
  m_world.reset(new World(*m_pool, m_blocks, config,
    [this](int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t) {
      m_genPool->submitAndForget([this, slotIndex, chunkX, chunkZ]() {
        auto slot = m_pool->view(slotIndex);
        // @see notes/worldgen-threadsafe-chunk-rng.md
        m_worldGenPipeline.generate(slot.voxels, chunkX, chunkZ,
          m_config.chunkSize, m_config.worldHeight, m_config.chunkSize,
          chunkSeed(chunkX, chunkZ, m_config.worldSeed));
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
        {
          std::lock_guard lock(m_completionMutex);
          m_completedGenSlots.push(slotIndex);
        }
      });
    },
    [this](int32_t slotIndex) {
      m_meshPool->submitAndForget([this, slotIndex]() {
        auto slot = m_pool->view(slotIndex);
        mesher::MesherConfig mcfg;
        mcfg.sizeX = m_config.chunkSize;
        mcfg.sizeY = m_config.worldHeight;
        mcfg.sizeZ = m_config.chunkSize;
        mcfg.maxVertices = m_config.maxVertsPerChunk;
        mcfg.maxIndices = m_config.maxIndicesPerChunk;
        mcfg.strideFloats = m_config.vertexStrideFloats;

        uint32_t vc = 0, ic = 0;
        bool hasTransparent = false;
        bool ok = mesher::greedyMesh(
            slot.voxels, slot.light, m_blocks, mcfg,
            slot.vertices, slot.indices, vc, ic,
            &hasTransparent);
        *slot.vertexCount = static_cast<uint32_t>(vc);
        *slot.indexCount = ic;
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
        if (hasTransparent) {
          *slot.status |= 0x10000;
        }
        {
          std::lock_guard lock(m_completionMutex);
          m_completedMeshSlots.push(slotIndex);
        }
        (void)ok;
      });
    },
    // Save load callback
    [this](int32_t cx, int32_t cz) {
      if (m_saveManager) m_saveManager->requestLoad(cx, cz);
    },
    // Mark dirty callback
    [this](int32_t cx, int32_t cz) {
      if (m_saveManager) m_saveManager->markDirty(cx, cz);
    }
  ));

  m_renderer = std::make_unique<Renderer>(window, m_blocks, config);

  m_saveDir = options.saveDir;
  configureSaveWorld(options.saveSlotId, false);

  m_audioRegistry.seedBuiltinSounds();
  m_blockAudio = std::make_unique<BlockInteractionAudio>(m_audioEngine, m_audioRegistry, m_blocks);

  m_ui = std::make_unique<UIManager>(window, UIManager::Callbacks{
    .onStartWorld = [this](GameMode mode, const std::string& slotId, bool startFresh) {
      startWorld(mode, slotId, startFresh);
    },
    .onQuitToTitle = [this]{ m_session.returnToTitle(); },
    .onResume = [this]{ glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); },
    .onQuit = [this]{ m_running = false; },
  });

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
  if (m_saveManager) m_saveManager->flushPending();
}

auto Game::makeRandomWorldSeed() -> uint32_t {
  return m_worldSeedRng();
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
    *m_world, m_camera, m_config, *m_ui, m_session,
    m_cameraDirty);
  // Keep a non-owning pointer for direct access (e.g. pushPlayerOutOfBlocks).
  m_playerController = controller.get();
  m_systems.add(std::move(controller));

  // --- Player spawn (runs once when terrain is ready) ---
  m_systems.add(std::make_unique<PlayerSpawnSystem>(
    m_transforms, m_bodies, *m_world, m_config,
    m_spawnedToSurface, m_cameraDirty, m_playerController));
}

void Game::configureSaveWorld(const std::string& slotId, bool startFresh) {
  std::string selectedSlot = slotId.empty() ? "default" : slotId;

  if (m_saveManager && !startFresh) {
    m_saveManager->flushPending();
  }
  m_saveManager.reset();

  if (m_world) {
    m_world->clear();
  }

  if (startFresh) {
    std::error_code ec;
    std::filesystem::remove_all(m_saveDir + "/" + selectedSlot, ec);
  }

  m_saveManager = std::make_unique<SaveManager>(m_saveDir, selectedSlot, *m_pool, *m_world, m_ioPool.get());
  m_world->attachSaveManager(m_saveManager.get());
}

void Game::startWorld(GameMode mode, const std::string& slotId, bool startFresh) {
  if (startFresh) {
    m_config.worldSeed = makeRandomWorldSeed();
    m_worldGenPipeline = WorldGenPipeline(m_config.worldSeed);
  }

  configureSaveWorld(slotId, startFresh);
  m_session.startSingleplayer(mode);
  m_dayNightCycle.setTime(daynight::kMiddayTimeSeconds);

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

  m_ui->clearUI();
  m_ui->setInventoryOpen(false);
  glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
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


void Game::processGenJobs() {
  // Drain completed gen jobs from the lock-free completion queue.
  // Workers push slot indices after setting VOXELS_READY status.
  std::queue<int32_t> genSlots;
  std::queue<int32_t> meshSlots;
  {
    std::lock_guard lock(m_completionMutex);
    genSlots.swap(m_completedGenSlots);
    meshSlots.swap(m_completedMeshSlots);
  }

  while (!genSlots.empty()) {
    int32_t i = genSlots.front();
    genSlots.pop();
    auto* chunk = m_world->getChunkBySlotIndex(i);
    if (chunk && chunk->state == ChunkState::Generating) {
      m_world->onWorldGenDone(i);
    }
  }

  while (!meshSlots.empty()) {
    int32_t i = meshSlots.front();
    meshSlots.pop();
    auto* chunk = m_world->getChunkBySlotIndex(i);
    if (chunk && chunk->state == ChunkState::Meshing) {
      auto slot = m_pool->view(i);
      // Extract transparent flag from the high bit of status
      int32_t rawStatus = *slot.status;
      chunk->hasTransparent = (rawStatus & 0x10000) != 0;
      m_world->onMeshDone(i, *slot.vertexCount, *slot.indexCount, true);
    }
  }
}

void Game::update(float dt) {
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::GeneratingWorld) {
    m_dayNightCycle.advance(dt);

    processGenJobs();
    if (m_saveManager) m_saveManager->processPending();
    m_world->update(m_camera.position);

    m_systems.update(*this, dt);

    if (m_session.state() == GameState::GeneratingWorld && m_world->isReady()) {
      m_session.markWorldReady();
    }
  }
}

void Game::render(float, float) {
  const float worldTimeSeconds = m_dayNightCycle.time();
  updateCamera();
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::Paused ||
      m_session.state() == GameState::GeneratingWorld) {
    float daylightFactor = m_dayNightCycle.daylight();
    m_renderer->render(*m_world, m_camera, worldTimeSeconds, daylightFactor);
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
