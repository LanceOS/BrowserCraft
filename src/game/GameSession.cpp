#include "GameSession.hpp"
#include "engine/core/RenderDistanceLimits.hpp"
#include <algorithm>

namespace terrain {

GameSession::GameSession(int32_t renderDistance)
  : m_renderDistance(clampRenderDistance(renderDistance)) {}

void GameSession::enterMainMenu() { m_state = GameState::MainMenu; }

void GameSession::startSingleplayer(GameMode mode) {
  m_gameMode = mode;
  m_state = GameState::GeneratingWorld;
  ++m_startRequestId;
}

auto GameSession::markWorldReady() -> bool {
  if (m_state != GameState::GeneratingWorld) return false;
  m_state = GameState::InGame;
  return true;
}

auto GameSession::pause() -> bool {
  if (m_state != GameState::InGame) return false;
  m_state = GameState::Paused;
  return true;
}

auto GameSession::resume() -> bool {
  if (m_state != GameState::Paused) return false;
  m_state = GameState::InGame;
  return true;
}

void GameSession::returnToTitle() { m_state = GameState::MainMenu; }

void GameSession::setRenderDistance(int32_t rd) { m_renderDistance = clampRenderDistance(rd); }

auto GameSession::clampRenderDistance(int32_t rd) -> int32_t {
  return std::clamp(rd, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
}

} // namespace terrain
