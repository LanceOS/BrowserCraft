#include "game/Game.hpp"
#include "engine/alloc/SharedPool.hpp"
#include "engine/workers/mesher/GreedyMesher.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>

namespace voxel {

namespace {
constexpr float kDayCycleSeconds = 3600.0f;
constexpr float kSunAngularSpeed = 6.28318530718f / kDayCycleSeconds;
constexpr float kMiddayTimeSeconds = kDayCycleSeconds * 0.5f;
constexpr float kNightLightMin = 0.20f;
constexpr float kMouseSensitivity = 0.0025f;
constexpr float kMaxPitch = 1.553343f; // ~89 degrees
constexpr float kJumpSpeed = 8.0f;
constexpr float kSwimSpeed = 3.5f;

auto computeDaylightFactor(float timeSeconds) -> float {
  float angle = timeSeconds * kSunAngularSpeed - 1.57079632679f; // -pi/2
  float daylight = std::sin(angle) * 0.5f + 0.5f;
  daylight = std::max(0.0f, std::min(1.0f, daylight));

  return kNightLightMin + (1.0f - kNightLightMin) * daylight;
}

} // namespace

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
        mesher::MesherConfig mcfg;
        mcfg.sizeX = m_config.chunkSize;
        mcfg.sizeY = m_config.worldHeight;
        mcfg.sizeZ = m_config.chunkSize;
        mcfg.maxVertices = m_config.maxVertsPerChunk;
        mcfg.maxIndices = m_config.maxIndicesPerChunk;
        mcfg.strideFloats = m_config.vertexStrideFloats;

        uint32_t vc = 0, ic = 0;
        bool ok = mesher::greedyMesh(
            slot.voxels, slot.light, m_blocks, mcfg,
            slot.vertices, slot.indices, vc, ic);
        *slot.vertexCount = static_cast<uint32_t>(vc);
        *slot.indexCount = ic;
        *slot.status = static_cast<int32_t>(ChunkSlotStatus::MESH_READY);
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

void Game::initSystems() {}

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

  m_saveManager = std::make_unique<SaveManager>(m_saveDir, selectedSlot, *m_pool, *m_world);
  m_world->attachSaveManager(m_saveManager.get());
}

void Game::startWorld(GameMode mode, const std::string& slotId, bool startFresh) {
  if (startFresh) {
    m_config.worldSeed = makeRandomWorldSeed();
    m_worldGenPipeline = WorldGenPipeline(m_config.worldSeed);
  }

  configureSaveWorld(slotId, startFresh);
  m_session.startSingleplayer(mode);
  m_worldTimeSeconds = kMiddayTimeSeconds;

  m_spawnedToSurface = false;
  m_camera.position = glm::vec3(0.0f, 80.0f, 50.0f);
  syncPlayerWithCamera();

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

void Game::syncCameraFromPlayer() {
  const int32_t idx = playerIndex();
  if (idx < 0) return;
  if (!m_transforms.has(idx) || !m_bodies.has(idx) || !m_players.has(idx)) return;

  const auto& transform = m_transforms.get(idx);
  const auto& player = m_players.get(idx);

  float cp = std::cos(player.pitch);
  m_camera.forward = glm::normalize(glm::vec3(
    cp * std::sin(player.yaw),
    std::sin(player.pitch),
    -cp * std::cos(player.yaw)
  ));
  m_camera.right = glm::normalize(glm::cross(m_camera.forward, m_camera.up));
  m_camera.position = transform.position + glm::vec3(0.0f, player.eyeHeight, 0.0f);
}

auto Game::groundHeightAt(float worldX, float worldZ, int32_t startY) const -> int32_t {
  if (!m_world) return -1;
  int32_t x = static_cast<int32_t>(std::floor(worldX));
  int32_t z = static_cast<int32_t>(std::floor(worldZ));
  int32_t y = std::clamp(startY, 0, m_config.worldHeight - 1);

  for (; y >= 0; --y) {
    if (m_world->isSolid(x, y, z)) return y;
  }
  return -1;
}

auto Game::collidesAt(const glm::vec3& candidatePosition, const cmp::RigidBody& body) const -> bool {
  if (!m_world) return false;

  const glm::vec3 minPoint = candidatePosition + body.aabbMin;
  const glm::vec3 maxPoint = candidatePosition + body.aabbMax;

  int32_t minX = static_cast<int32_t>(std::floor(minPoint.x));
  int32_t maxX = static_cast<int32_t>(std::floor(maxPoint.x));
  int32_t minY = static_cast<int32_t>(std::floor(minPoint.y));
  int32_t maxY = static_cast<int32_t>(std::floor(maxPoint.y));
  int32_t minZ = static_cast<int32_t>(std::floor(minPoint.z));
  int32_t maxZ = static_cast<int32_t>(std::floor(maxPoint.z));

  for (int32_t y = minY; y <= maxY; ++y) {
    for (int32_t z = minZ; z <= maxZ; ++z) {
      for (int32_t x = minX; x <= maxX; ++x) {
        if (m_world->isSolid(x, y, z)) return true;
      }
    }
  }
  return false;
}

void Game::applyPlayerControls(float dt) {
  if (m_session.state() != GameState::InGame) return;

  const int32_t idx = playerIndex();
  if (idx < 0 ||
      !m_transforms.has(idx) || !m_bodies.has(idx) || !m_players.has(idx)) {
    return;
  }

  auto& transform = m_transforms.get(idx);
  auto& body = m_bodies.get(idx);
  auto& player = m_players.get(idx);

  if (m_input.isPressed(InputState::KEY_E)) {
    bool nowOpen = !m_ui->isInventoryOpen();
    m_ui->setInventoryOpen(nowOpen);
    glfwSetInputMode(m_window, GLFW_CURSOR, nowOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  }

  const bool canControl = m_session.state() == GameState::InGame &&
    (m_ui->state() == UIState::InGame) &&
    !m_ui->isInventoryOpen();

  if (!canControl) {
    body.velocity.x = 0.0f;
    body.velocity.z = 0.0f;
    return;
  }

  if (glfwGetInputMode(m_window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  }

  m_input.pointerLocked = true;
  player.yaw += m_input.mouseDX() * kMouseSensitivity;
  player.pitch -= m_input.mouseDY() * kMouseSensitivity;
  player.pitch = std::clamp(player.pitch, -kMaxPitch, kMaxPitch);

  glm::vec3 forwardFlat(std::sin(player.yaw), 0.0f, -std::cos(player.yaw));
  forwardFlat = glm::normalize(forwardFlat);
  glm::vec3 rightFlat = glm::normalize(glm::cross(forwardFlat, m_camera.up));

  glm::vec3 moveDir(0.0f);
  if (m_input.isHeld(InputState::KEY_W)) moveDir += forwardFlat;
  if (m_input.isHeld(InputState::KEY_S)) moveDir -= forwardFlat;
  if (m_input.isHeld(InputState::KEY_A)) moveDir -= rightFlat;
  if (m_input.isHeld(InputState::KEY_D)) moveDir += rightFlat;

  if (glm::dot(moveDir, moveDir) > 0.0f) moveDir = glm::normalize(moveDir);

  if (player.isFlying) {
    float speed = player.flySpeed;
    if (m_input.isHeld(InputState::KEY_SPACE)) {
      transform.position.y += speed * dt;
    }
    if (m_input.isHeld(InputState::KEY_CTRL)) {
      transform.position.y -= speed * dt;
    }
    transform.position += moveDir * speed * dt;
    body.velocity = glm::vec3(0.0f);
    body.onGround = 0;
  } else {
    float speed = m_input.isHeld(InputState::KEY_SHIFT) ? player.sprintSpeed : player.walkSpeed;

    if (m_input.isPressed(InputState::KEY_SPACE) && (body.onGround || body.isFluid)) {
      body.velocity.y = body.isFluid ? kSwimSpeed : kJumpSpeed;
      body.onGround = 0;
      body.isFluid = 0;
    }

    body.velocity.y -= body.gravity * dt;
    body.velocity.x = 0.0f;
    body.velocity.z = 0.0f;

    glm::vec3 nextPosition = transform.position;

    glm::vec3 xStep = nextPosition + glm::vec3(moveDir.x * speed * dt, 0.0f, 0.0f);
    if (!collidesAt(xStep, body)) {
      nextPosition.x = xStep.x;
    } else {
      body.velocity.x = 0.0f;
    }

    glm::vec3 zStep = nextPosition + glm::vec3(0.0f, 0.0f, moveDir.z * speed * dt);
    if (!collidesAt(zStep, body)) {
      nextPosition.z = zStep.z;
    } else {
      body.velocity.z = 0.0f;
    }

    glm::vec3 yStep = nextPosition + glm::vec3(0.0f, body.velocity.y * dt, 0.0f);
    if (!collidesAt(yStep, body)) {
      nextPosition = yStep;
      body.onGround = 0;
    } else if (body.velocity.y <= 0.0f) {
      int32_t groundY = groundHeightAt(nextPosition.x, nextPosition.z, std::max(0, static_cast<int32_t>(std::floor(nextPosition.y + body.aabbMin.y))));
      if (groundY >= 0) {
        nextPosition.y = static_cast<float>(groundY + 1);
        body.onGround = 1;
      } else {
        body.onGround = 0;
      }
      body.velocity.y = 0.0f;
    } else {
      body.velocity.y = 0.0f;
    }

    transform.position = nextPosition;

    int32_t sampleY = std::max(0, static_cast<int32_t>(std::floor(transform.position.y + body.aabbMin.y)));
    int32_t sampleX = static_cast<int32_t>(std::floor(transform.position.x));
    int32_t sampleZ = static_cast<int32_t>(std::floor(transform.position.z));
    body.isFluid = m_world->isFluid(sampleX, sampleY, sampleZ);
    if (body.isFluid) {
      body.velocity.y = std::min(body.velocity.y, 0.0f);
    }
  }

  if (m_world->isReady()) {
    body.isFluid = m_world->isFluid(
      static_cast<int32_t>(std::floor(transform.position.x)),
      std::max(0, static_cast<int32_t>(std::floor(transform.position.y + body.aabbMin.y))),
      static_cast<int32_t>(std::floor(transform.position.z)));
  }

  syncCameraFromPlayer();
}

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
    m_worldTimeSeconds += dt;
    if (m_worldTimeSeconds >= kDayCycleSeconds) {
      m_worldTimeSeconds -= kDayCycleSeconds;
    }

    if (m_session.state() == GameState::InGame) {
      applyPlayerControls(dt);
      syncCameraFromPlayer();
    }

    processGenJobs();

    if (!m_spawnedToSurface && m_world->isReady()) {
      int32_t idx = playerIndex();
    if (idx >= 0 && m_transforms.has(idx)) {
        auto& transform = m_transforms.get(idx);
        auto& body = m_bodies.get(idx);
        const int32_t groundY = groundHeightAt(transform.position.x, transform.position.z, m_config.worldHeight - 1);
        m_spawnedToSurface = true;
        if (groundY >= 0) {
          float spawnY = static_cast<float>(groundY + 1);
          transform.position.y = spawnY;
          while (collidesAt(transform.position, body)) {
            transform.position.y += 0.05f;
          }
          body.velocity.y = 0.0f;
          body.onGround = 1;
        } else {
          transform.position.y = std::min(transform.position.y, 64.0f);
          body.velocity.y = 0.0f;
          body.onGround = 0;
        }
        syncCameraFromPlayer();
      }
    }

    m_world->update(m_camera.position);
    m_systems.update(*this, dt);

    if (m_session.state() == GameState::GeneratingWorld && m_world->isReady()) {
      m_session.markWorldReady();
    }
  }
}

void Game::render(float, float) {
  const float worldTimeSeconds = m_worldTimeSeconds;
  updateCamera();
  if (m_session.state() == GameState::InGame ||
      m_session.state() == GameState::Paused ||
      m_session.state() == GameState::GeneratingWorld) {
    float daylightFactor = computeDaylightFactor(worldTimeSeconds);
    m_renderer->render(*m_world, m_camera, worldTimeSeconds, daylightFactor);
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
      render(0.0f, m_worldTimeSeconds);
      m_ui->endFrame();

      glfwSwapBuffers(m_window);
      m_input.clearFrameState();
    }
  );
  loop.run();
}

} // namespace voxel
