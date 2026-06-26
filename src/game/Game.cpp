#include "game/GameOrchestrator.hpp"

namespace voxel {

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
  GameOrchestrator::initialize(*this, window, options);
}

Game::~Game() {
  GameOrchestrator::shutdown(*this);
}

void Game::applyRenderDistance(int32_t newRd) {
  GameOrchestrator::applyRenderDistance(*this, newRd);
}

void Game::update(float dt) {
  GameOrchestrator::update(*this, dt);
}

void Game::render(float alpha, float timeSeconds) {
  GameOrchestrator::render(*this, alpha, timeSeconds);
}

void Game::run() {
  GameOrchestrator::run(*this);
}

} // namespace voxel
