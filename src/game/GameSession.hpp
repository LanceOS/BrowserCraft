#pragma once

#include "engine/core/GameState.hpp"
#include "game/GameMode.hpp"
#include <cstdint>
#include <string>

namespace voxel {

/// Tracks the high-level game session state (menu, playing, paused, etc.).
class GameSession {
public:
  explicit GameSession(int32_t renderDistance);

  [[nodiscard]] auto state() const -> GameState { return m_state; }
  [[nodiscard]] auto renderDistance() const -> int32_t { return m_renderDistance; }
  [[nodiscard]] auto gameMode() const -> GameMode { return m_gameMode; }
  [[nodiscard]] auto startRequestId() const -> int32_t { return m_startRequestId; }

  void enterMainMenu();
  void startSingleplayer(GameMode mode);
  auto markWorldReady() -> bool;
  auto pause() -> bool;
  auto resume() -> bool;
  void returnToTitle();
  void setRenderDistance(int32_t rd);

private:
  static auto clampRenderDistance(int32_t rd) -> int32_t;

  GameState m_state = GameState::Booting;
  int32_t m_renderDistance;
  GameMode m_gameMode = GameMode::Survival;
  int32_t m_startRequestId = 0;
};

} // namespace voxel
