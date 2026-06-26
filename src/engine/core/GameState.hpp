#pragma once

namespace terrain {

/// High-level game state for UI and loop control.
enum class GameState {
  Booting,
  MainMenu,
  GeneratingWorld,
  InGame,
  Paused,
};

} // namespace terrain
