#pragma once

#include <array>
#include <string>
#include <vector>
#include <functional>
#include <GLFW/glfw3.h>
#include "game/GameSession.hpp"

namespace voxel {

enum class UIState {
  MainMenu,
  GameModeMenu,
  Options,
  InGame,
  Paused,
  Inventory,
};

/// A lightweight representation of a saved world for UI rendering.
struct WorldEntry {
  std::string name;
  std::string slug;
  uint32_t seed = 0;
  int32_t gameMode = 0; // 0 = Survival, 1 = Creative
  int64_t lastPlayedTimestamp = 0;
};

/// ImGui-based UI manager for menus, HUD, and inventory.
class UIManager {
public:
  using Callback = std::function<void()>;
  using StartWorldCallback = std::function<void(GameMode, const std::string&, bool)>;

  struct Callbacks {
    StartWorldCallback onStartWorld;
    Callback onQuitToTitle;
    Callback onResume;
    Callback onQuit;
    Callback onRenderDistanceChanged;
  };

  UIManager(GLFWwindow* window, Callbacks callbacks);
  ~UIManager();

  UIManager(const UIManager&) = delete;
  UIManager& operator=(const UIManager&) = delete;

  /// Render ImGui frames. Call before Renderer::render each frame.
  void beginFrame();

  /// Finish ImGui rendering. Call after Renderer::render each frame.
  void endFrame();

  /// Show the main menu.
  void showMainMenu();

  /// Show the pause menu.
  void showPauseMenu();

  /// Show the options menu.
  void showOptions();

  /// Hide all menus (in-game state).
  void clearUI();

  /// Handle a named action.
  void handleAction(const std::string& action);

  [[nodiscard]] auto state() const -> UIState { return m_state; }
  void setState(UIState s) { m_state = s; }

  /// Render the hotbar HUD at the bottom of the screen.
  void renderHotbar(int32_t selectedSlot);

  /// Render inventory panel.
  void renderInventory(bool open);

  /// Show or hide the FPS counter overlay.
  void setShowFps(bool show) { m_showFps = show; }
  [[nodiscard]] auto isFpsVisible() const -> bool { return m_showFps; }

  [[nodiscard]] auto isInventoryOpen() const -> bool { return m_inventoryOpen; }
  void setInventoryOpen(bool open) { m_inventoryOpen = open; }

  [[nodiscard]] auto wantsPointerLock() const -> bool {
    return m_state == UIState::InGame && !m_inventoryOpen;
  }

  // ---- World list integration ----

  /// Supply the current list of saved worlds (called from Game each frame).
  void setWorldList(std::vector<WorldEntry> worlds);

  /// Get the current world list.
  [[nodiscard]] auto worldList() const -> const std::vector<WorldEntry>& { return m_worldEntries; }

  /// Set an error/status message to display in the game mode menu.
  void setWorldError(std::string msg) { m_worldErrorMsg = std::move(msg); }

  /// Clear the error message.
  void clearWorldError() { m_worldErrorMsg.clear(); }

private:
  void renderMainMenu();
  void renderGameModeMenu();
  void renderPauseMenu();
  void renderFpsOverlay();
  void renderOptionsMenu();

  /// Format a timestamp into a human-readable date string.
  static auto formatTimestamp(int64_t timestamp) -> std::string;

  GLFWwindow* m_window;
  Callbacks m_callbacks;
  UIState m_state = UIState::MainMenu;
  bool m_inventoryOpen = false;
  // FPS counter
  double m_fpsLastSampleTime = 0.0;
  int m_fpsFrameCount = 0;
  float m_currentFps = 0.0f;
  bool m_showFps = true;

  int32_t m_renderDistance = 8;
  int32_t m_gameModeIndex = 0; // 0 = Survival, 1 = Creative
  std::array<char, 64> m_slotId{};
  bool m_showDemoWindow = false;
  bool m_initialized = false;

  // World list state
  std::vector<WorldEntry> m_worldEntries;
  std::string m_worldErrorMsg;
};

} // namespace voxel
