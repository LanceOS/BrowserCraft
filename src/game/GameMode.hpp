#pragma once

namespace terrain {

/// Player-facing game mode selection shared by save/load, UI, and session state.
enum class GameMode {
  Survival,
  Creative,
};

} // namespace terrain
