#pragma once

#include "game/Game.hpp"

namespace voxel {

class GameOrchestrator {
public:
  static void initialize(Game& game, GLFWwindow* window, const Game::Options& options);
  static void shutdown(Game& game);
  static void applyRenderDistance(Game& game, int32_t newRd);
  static void update(Game& game, float dt);
  static void render(Game& game, float alpha, float timeSeconds);
  static void run(Game& game);

private:
  static void buildRuntimeStack(Game& game);
  static void initECS(Game& game);
  static void initSystems(Game& game);
  static void createPlayer(Game& game);
  [[nodiscard]] static auto playerIndex(const Game& game) -> int32_t;
  static void syncPlayerWithCamera(Game& game);
  static void startWorld(Game& game, GameMode mode, const std::string& slotId, bool startFresh);
  static void updateCamera(Game& game);
};

} // namespace voxel
