#include "game/Game.hpp"
#include "engine/alloc/SharedPool.hpp"
#include <thread>

namespace voxel {

static auto makeDims(const GameConfig& cfg) -> ChunkDimensions {
  return {cfg.chunkSize, cfg.worldHeight, cfg.chunkSize,
          cfg.maxVertsPerChunk, cfg.maxIndicesPerChunk, cfg.vertexStrideFloats};
}

// ---- Block registration ----
static void registerVanillaBlocks(BlockRegistry& blocks) {
  blocks.register_(BlockDefinition{.id=1,.name="stone",.textures={1,1,1},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=2,.name="grass",.textures={2,3,4},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=3,.name="dirt",.textures={4,4,4},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=4,.name="cobblestone",.textures={5,5,5},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=5,.name="wood_planks",.textures={6,6,6},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=7,.name="bedrock",.textures={19,19,19},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=8,.name="water",.textures={20,20,20},
    .material={.opaque=false,.transparent=true,.liquid=true},.collision=EMPTY_BLOCK_AABB});
  blocks.register_(BlockDefinition{.id=9,.name="still_water",.textures={20,20,20},
    .material={.opaque=false,.transparent=true,.liquid=true},.collision=EMPTY_BLOCK_AABB});
  blocks.register_(BlockDefinition{.id=10,.name="lava",.textures={21,21,21},
    .material={.opaque=false,.transparent=true,.liquid=true,.lightEmission=15}});
  blocks.register_(BlockDefinition{.id=12,.name="sand",.textures={11,11,11},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=14,.name="gold_ore",.textures={15,15,15},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=15,.name="iron_ore",.textures={14,14,14},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=16,.name="coal_ore",.textures={13,13,13},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=17,.name="log",.textures={7,7,8},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=18,.name="leaves",.textures={9,9,9},
    .material={.opaque=false,.transparent=true,.foliage=true}});
  blocks.register_(BlockDefinition{.id=20,.name="glass",.textures={10,10,10},
    .material={.opaque=false,.transparent=true}});
  blocks.register_(BlockDefinition{.id=41,.name="gold_block",.textures={36,36,36},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=42,.name="iron_block",.textures={37,37,37},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=49,.name="obsidian",.textures={27,27,27},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=56,.name="diamond_ore",.textures={16,16,16},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=57,.name="diamond_block",.textures={35,35,35},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=58,.name="crafting_table",.textures={28,6,29},.material={.opaque=true}});
  blocks.register_(BlockDefinition{.id=89,.name="glowstone",.textures={43,43,43},
    .material={.opaque=true,.lightEmission=15}});
}

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
  registerVanillaBlocks(m_blocks);

  // Thread pool (use hardware concurrency, min 1)
  int32_t threads = std::max(1u, std::thread::hardware_concurrency());
  m_genPool = std::make_unique<WorkerThreadPool>(std::max(1, threads / 2));
  m_meshPool = std::make_unique<WorkerThreadPool>(std::max(1, threads / 2));

  int32_t poolCap = (MAX_RENDER_DISTANCE * 2 + 1) * (MAX_RENDER_DISTANCE * 2 + 1) + 8;
  m_pool = SharedPool::create(poolCap, makeDims(config));

  // Wire world gen callback through thread pool
  m_world.reset(new World(*m_pool, m_blocks, config,
    [this](int32_t slotIndex, int32_t chunkX, int32_t chunkZ, uint32_t) {
      m_genPool->submit([this, slotIndex, chunkX, chunkZ]() {
        auto slot = m_pool->view(slotIndex);
        m_worldGenPipeline.generate(slot.voxels, chunkX, chunkZ,
          m_config.chunkSize, m_config.worldHeight, m_config.chunkSize);
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY);
      });
    },
    [this](int32_t slotIndex) {
      m_meshPool->submit([this, slotIndex]() {
        auto slot = m_pool->view(slotIndex);
        // Stub mesh: create empty mesh for now
        *slot.vertexCount = 0;
        *slot.indexCount = 0;
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
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

  m_saveManager = std::make_unique<SaveManager>(options.saveDir, options.saveSlotId, *m_pool, *m_world);
  m_world->attachSaveManager(m_saveManager.get());

  m_audioRegistry.seedBuiltinSounds();
  m_blockAudio = std::make_unique<BlockInteractionAudio>(m_audioEngine, m_audioRegistry, m_blocks);

  m_ui = std::make_unique<UIManager>(window, UIManager::Callbacks{
    .onStartSurvival = [this]{
      m_session.startSingleplayer(GameMode::Survival);
      m_ui->clearUI();
      glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    },
    .onStartCreative = [this]{
      m_session.startSingleplayer(GameMode::Creative);
      m_ui->clearUI();
      glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
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
}

Game::~Game() {
  if (m_saveManager) m_saveManager->flushPending();
}

void Game::initECS() { createPlayer(); }

void Game::createPlayer() {
  m_playerEntityId = m_entityManager.allocate();
  int32_t idx = EntityManager::indexOf(m_playerEntityId);
  m_transforms.add(idx);
  m_bodies.add(idx);
  m_players.add(idx);
}

void Game::initSystems() {}

void Game::processGenJobs() {
  // Check completed gen jobs and notify world
  for (int32_t i = 0; i < m_pool->capacity(); ++i) {
    auto slot = m_pool->view(i);
    if (*slot.status == static_cast<int32_t>(ChunkSlotStatus::VOXELS_READY)) {
      auto* chunk = m_world->getChunkBySlotIndex(i);
      if (chunk && chunk->state == ChunkState::Generating) {
        m_world->onWorldGenDone(i);
      }
    }
    if (*slot.status == static_cast<int32_t>(ChunkSlotStatus::MESH_READY)) {
      auto* chunk = m_world->getChunkBySlotIndex(i);
      if (chunk && chunk->state == ChunkState::Meshing) {
        m_world->onMeshDone(i, *slot.vertexCount, *slot.indexCount, true);
      }
    }
  }
}

void Game::update(float dt) {
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::GeneratingWorld) {
    processGenJobs();
    m_world->update(m_camera.position);
    m_systems.update(*this, dt);

    if (m_session.state() == GameState::GeneratingWorld && m_world->isReady()) {
      m_session.markWorldReady();
    }
  }
}

void Game::render(float, float timeSeconds) {
  updateCamera();
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::Paused ||
      m_session.state() == GameState::GeneratingWorld) {
    m_renderer->render(*m_world, m_camera, timeSeconds, 1.0f);
  } else {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void Game::updateCamera() {
  m_camera.viewMatrix = glm::lookAt(m_camera.position, m_camera.position + m_camera.forward, m_camera.up);
  m_camera.viewProjectionMatrix = m_camera.projectionMatrix * m_camera.viewMatrix;
  m_camera.inverseViewProjectionMatrix = glm::inverse(m_camera.viewProjectionMatrix);
}

void Game::run() {
  GameLoop loop(60.0f,
    [this](float dt) { update(dt); },
    [this, &loop](float, float time) {
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
      render(0.0f, time);
      m_ui->endFrame();

      glfwSwapBuffers(m_window);
      m_input.clearFrameState();
    }
  );
  loop.run();
}

} // namespace voxel
