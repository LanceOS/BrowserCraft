#pragma once

#include <array>
#include <string>
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

  [[nodiscard]] auto isInventoryOpen() const -> bool { return m_inventoryOpen; }
  void setInventoryOpen(bool open) { m_inventoryOpen = open; }

  [[nodiscard]] auto wantsPointerLock() const -> bool {
    return m_state == UIState::InGame && !m_inventoryOpen;
  }

private:
  void renderMainMenu();
  void renderGameModeMenu();
  void renderPauseMenu();
  void renderOptionsMenu();

  GLFWwindow* m_window;
  Callbacks m_callbacks;
  UIState m_state = UIState::MainMenu;
  bool m_inventoryOpen = false;
  int32_t m_renderDistance = 8;
  int32_t m_gameModeIndex = 0; // 0 = Survival, 1 = Creative
  std::array<char, 64> m_slotId{};
  bool m_showDemoWindow = false;
  bool m_initialized = false;
};

} // namespace voxel
