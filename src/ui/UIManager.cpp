#include "UIManager.hpp"
#include "engine/core/RenderDistanceLimits.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <chrono>

namespace {
auto sanitizeSlotId(std::string slotId) -> std::string {
  auto isInvalid = [](unsigned char ch) {
    return ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
           ch == '"' || ch == '<' || ch == '>' || ch == '|';
  };

  slotId.erase(slotId.begin(), std::find_if(slotId.begin(), slotId.end(),
    [](unsigned char c){ return !std::isspace(c); }));

  slotId.erase(std::find_if(slotId.rbegin(), slotId.rend(),
    [](unsigned char c){ return !std::isspace(c); }).base(), slotId.end());

  for (auto& ch : slotId) {
    unsigned char u = static_cast<unsigned char>(ch);
    if (std::isspace(u) || isInvalid(u) || u < 0x20u) {
      ch = '_';
    }
  }

  return slotId;
}
auto setupImGuiStyle() -> void {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 6.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 6.0f;
  style.WindowPadding = ImVec2(16.0f, 16.0f);
  style.FramePadding = ImVec2(12.0f, 6.0f);
  style.ItemSpacing = ImVec2(12.0f, 8.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
  colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.12f, 0.14f, 0.85f);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.13f, 0.15f, 0.18f, 0.90f);
  colors[ImGuiCol_PopupBg]                = ImVec4(0.10f, 0.12f, 0.14f, 0.95f);
  colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
  colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.23f, 0.27f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
  colors[ImGuiCol_FrameBgActive]          = ImVec4(0.35f, 0.40f, 0.45f, 1.00f);
  colors[ImGuiCol_TitleBg]                = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
  colors[ImGuiCol_TitleBgActive]          = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
  colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
  colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
  colors[ImGuiCol_CheckMark]              = ImVec4(0.30f, 0.70f, 0.90f, 1.00f);
  colors[ImGuiCol_SliderGrab]             = ImVec4(0.30f, 0.70f, 0.90f, 1.00f);
  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.35f, 0.80f, 0.95f, 1.00f);
  colors[ImGuiCol_Button]                 = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);
  colors[ImGuiCol_ButtonHovered]          = ImVec4(0.35f, 0.40f, 0.45f, 1.00f);
  colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
  colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
  colors[ImGuiCol_HeaderHovered]          = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
  colors[ImGuiCol_HeaderActive]           = ImVec4(0.35f, 0.40f, 0.45f, 1.00f);
  colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);
  colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.40f, 0.45f, 1.00f);
  colors[ImGuiCol_SeparatorActive]        = ImVec4(0.45f, 0.50f, 0.55f, 1.00f);
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
  colors[ImGuiCol_Tab]                    = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.35f, 0.40f, 0.45f, 1.00f);
  colors[ImGuiCol_TabActive]              = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
  colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.05f, 0.05f, 0.05f, 0.70f);
}
} // namespace

namespace terrain {

UIManager::UIManager(GLFWwindow* window, Callbacks callbacks)
  : m_window(window), m_callbacks(std::move(callbacks)) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr; // don't save layout
  std::strncpy(m_slotId.data(), "default", m_slotId.size() - 1);
  m_slotId[m_slotId.size() - 1] = '\0';
  setupImGuiStyle();

  ImGui_ImplGlfw_InitForOpenGL(window, false);
  ImGui_ImplOpenGL3_Init("#version 460");
  m_initialized = true;
  m_fpsLastSampleTime = glfwGetTime();
}

UIManager::~UIManager() {
  if (m_initialized) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
}

void UIManager::beginFrame() {
  // ---- FPS tracking ----
  m_fpsFrameCount++;
  double now = glfwGetTime();
  double elapsed = now - m_fpsLastSampleTime;
  if (elapsed >= 0.5) { // update twice per second
    m_currentFps = static_cast<float>(m_fpsFrameCount / elapsed);
    m_fpsFrameCount = 0;
    m_fpsLastSampleTime = now;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Delegate to the current state renderer
  switch (m_state) {
    case UIState::MainMenu:    renderMainMenu(); break;
    case UIState::GameModeMenu: renderGameModeMenu(); break;
    case UIState::Paused:      renderPauseMenu(); break;
    case UIState::Options:     renderOptionsMenu(); break;
    case UIState::InGame:
      renderHotbar(m_selectedHotbarSlot);
      renderInventory(m_inventoryOpen);
      if (m_showFps) renderFpsOverlay();
      break;
    case UIState::Inventory:   break;
  }
}

void UIManager::endFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::showMainMenu() { m_state = UIState::MainMenu; }
void UIManager::showPauseMenu() { m_state = UIState::Paused; }
void UIManager::showOptions() {
  m_previousState = m_state;
  m_state = UIState::Options;
}
void UIManager::clearUI() {
  m_state = UIState::InGame;
  m_inventoryOpen = false;
}

void UIManager::setSelectedHotbarSlot(int32_t slot) {
  if (slot < 0 || slot >= 9) {
    m_selectedHotbarSlot = -1;
    return;
  }
  m_selectedHotbarSlot = slot;
}

void UIManager::handleAction(const std::string& action) {
  if (action == "resume-game") {
    clearUI();
    if (m_callbacks.onResume) m_callbacks.onResume();
  } else if (action == "quit-to-title") {
    showMainMenu();
    if (m_callbacks.onQuitToTitle) m_callbacks.onQuitToTitle();
  } else if (action == "toggle-pause") {
    if (m_state == UIState::Paused) {
      clearUI();
      if (m_callbacks.onResume) m_callbacks.onResume();
    } else if (m_state == UIState::InGame) {
      showPauseMenu();
    }
  }
}

// ---- Private ImGui renderers ----

void UIManager::renderMainMenu() {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("MainMenu", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoSavedSettings);

  float winW = ImGui::GetWindowWidth();
  float winH = ImGui::GetWindowHeight();
  
  float panelW = 400.0f;
  float panelH = 340.0f;
  ImGui::SetCursorPos(ImVec2((winW - panelW) * 0.5f, (winH - panelH) * 0.5f));

  ImGui::BeginChild("MenuPanel", ImVec2(panelW, panelH), ImGuiChildFlags_Border);
  
  ImGui::Spacing();
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
  float titleW = ImGui::CalcTextSize("TERRAIN ENGINE").x;
  ImGui::SetCursorPosX((panelW - titleW) * 0.5f);
  ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "TERRAIN ENGINE");
  ImGui::PopFont();
  ImGui::Separator();
  ImGui::Spacing(); ImGui::Spacing();

  float btnW = 240.0f;
  float btnH = 45.0f;
  float btnStartX = (panelW - btnW) * 0.5f;

  if (m_sessionActive) {
    ImGui::SetCursorPosX(btnStartX);
    if (ImGui::Button("Resume", ImVec2(btnW, btnH))) {
      handleAction("resume-game");
    }
  } else {
    ImGui::SetCursorPosX(btnStartX);
    if (ImGui::Button("Start Game", ImVec2(btnW, btnH))) {
      m_state = UIState::GameModeMenu;
    }
  }
  
  ImGui::Spacing();
  ImGui::SetCursorPosX(btnStartX);
  if (ImGui::Button("Options", ImVec2(btnW, btnH))) {
    showOptions();
  }
  
  ImGui::Spacing();
  ImGui::SetCursorPosX(btnStartX);
  if (ImGui::Button("Quit", ImVec2(btnW, btnH))) {
    if (m_callbacks.onQuit) m_callbacks.onQuit();
  }
  ImGui::EndChild();

  ImGui::End();
}

void UIManager::renderGameModeMenu() {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("GameModeMenu", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);

  float winW = ImGui::GetWindowWidth();
  float winH = ImGui::GetWindowHeight();

  // Dynamically calculate height to fit worlds, up to 90% of screen height
  float requiredListH = m_worldEntries.empty() ? 100.0f : (m_worldEntries.size() * 100.0f);
  float panelW = 540.0f;
  float panelH = std::min(winH * 0.9f, 200.0f + requiredListH);
  
  ImGui::SetCursorPos(ImVec2((winW - panelW) * 0.5f, (winH - panelH) * 0.5f));

  ImGui::BeginChild("WorldListPanel", ImVec2(panelW, panelH), ImGuiChildFlags_Border);
  
  ImGui::Spacing();
  ImGui::SetCursorPosX((panelW - 240) * 0.5f);
  if (ImGui::Button("Create New World", ImVec2(240, 35))) {
    ImGui::OpenPopup("Create World");
  }
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  
  float textW = ImGui::CalcTextSize("Saved Worlds").x;
  ImGui::SetCursorPosX((panelW - textW) * 0.5f);
  ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Saved Worlds");
  ImGui::Separator();

  bool openDeletePopup = false;
  if (m_worldEntries.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "  No saved worlds found.");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "  Click 'Create New World' to get started.");
  } else {
    ImGui::BeginChild("WorldListScroll", ImVec2(0, -60.0f), ImGuiChildFlags_None);
    for (size_t i = 0; i < m_worldEntries.size(); ++i) {
      const auto& entry = m_worldEntries[i];

      ImGui::PushID(static_cast<int>(i));
      ImGui::Separator();

      // World name
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "  %s", entry.name.c_str());

      // Game mode + date on second line
      const char* modeStr = (entry.gameMode == 1) ? "Creative" : "Survival";
      ImGui::TextDisabled("  [%s]", modeStr);

      std::string dateStr = formatTimestamp(entry.lastPlayedTimestamp);
      ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 120);
      ImGui::TextDisabled("%s", dateStr.c_str());

      // Buttons
      ImGui::Spacing();
      ImGui::SetCursorPosX(16);
      float loadBtnW = ImGui::GetContentRegionAvail().x - 100;
      if (ImGui::Button("Load", ImVec2(loadBtnW, 30))) {
        if (m_callbacks.onStartWorld) {
          GameMode gm = (entry.gameMode == 1) ? GameMode::Creative : GameMode::Survival;
          m_callbacks.onStartWorld(gm, entry.slug, false);
        }
      }
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
      if (ImGui::Button("Delete", ImVec2(70, 30))) {
        m_worldToDelete = entry.slug;
        openDeletePopup = true;
      }
      ImGui::PopStyleColor(3);

      ImGui::PopID();
      ImGui::Spacing();
    }
    ImGui::EndChild();
  }

  if (openDeletePopup) {
    ImGui::OpenPopup("Delete World?");
  }

  // Delete Confirmation Modal
  ImGui::SetNextWindowPos(ImVec2(winW * 0.5f, winH * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Delete World?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::Text("Are you sure you want to delete this world?\nThis action cannot be undone.");
    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::Button("Cancel", ImVec2(120, 30))) {
      m_worldToDelete.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button("Delete", ImVec2(120, 30))) {
      if (m_callbacks.onDeleteWorld && !m_worldToDelete.empty()) {
        m_callbacks.onDeleteWorld(m_worldToDelete);
      }
      m_worldToDelete.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndPopup();
  }

  // Bottom action buttons
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::SetCursorPosX((panelW - 100) * 0.5f);
  if (ImGui::Button("Back", ImVec2(100, 30))) {
    m_state = UIState::MainMenu;
  }

  // Create World Modal
  ImGui::SetNextWindowPos(ImVec2(winW * 0.5f, winH * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Create World", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    float modalW = 400.0f;
    ImGui::Dummy(ImVec2(modalW, 0)); // enforce width
    
    // Game mode selection
    ImGui::Text("Game Mode");
    ImGui::RadioButton("Survival##create_mode", &m_gameModeIndex, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Creative##create_mode", &m_gameModeIndex, 1);
    
    ImGui::Spacing(); ImGui::Spacing();

    // World name input
    ImGui::Text("World Name");
    ImGui::PushItemWidth(modalW);
    ImGui::InputText("##world_name", m_slotId.data(), m_slotId.size());
    ImGui::PopItemWidth();

    const std::string slotId = sanitizeSlotId(std::string(m_slotId.data()));
    const bool hasValidSlot = !slotId.empty();
    const GameMode mode = m_gameModeIndex == 1 ? GameMode::Creative : GameMode::Survival;

    // Validation messages
    ImGui::Spacing();
    if (!hasValidSlot) {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "World name cannot be blank");
    } else if (slotId != std::string(m_slotId.data())) {
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Invalid characters replaced with '_'");
    } else {
      ImGui::Text(" "); // placeholder to prevent jitter
    }

    if (!m_worldErrorMsg.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_worldErrorMsg.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(120, 35))) {
      m_worldErrorMsg.clear();
      ImGui::CloseCurrentPopup();
    }
    
    ImGui::SameLine(modalW - 120 + 8); // 8 is padding roughly
    if (!hasValidSlot) ImGui::BeginDisabled();
    if (ImGui::Button("Start World", ImVec2(120, 35))) {
      m_worldErrorMsg.clear();
      if (m_callbacks.onStartWorld) {
        m_callbacks.onStartWorld(mode, slotId.empty() ? "default" : slotId, true);
        ImGui::CloseCurrentPopup();
      }
    }
    if (!hasValidSlot) ImGui::EndDisabled();

    ImGui::EndPopup();
  }

  ImGui::EndChild();
  ImGui::End();
}

void UIManager::setWorldList(std::vector<WorldEntry> worlds) {
  m_worldEntries = std::move(worlds);
}

auto UIManager::formatTimestamp(int64_t timestamp) -> std::string {
  if (timestamp <= 0) return "Never";

  auto tp = std::chrono::system_clock::from_time_t(timestamp);
  auto now = std::chrono::system_clock::now();
  auto diff = std::chrono::duration_cast<std::chrono::hours>(now - tp).count();

  if (diff < 24) return "Today";
  if (diff < 48) return "Yesterday";
  if (diff < 168) return std::to_string(diff / 24) + "d ago";

  // Format as date
  std::time_t t = static_cast<std::time_t>(timestamp);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
  return buf;
}

void UIManager::renderPauseMenu() {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("PauseMenu", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoSavedSettings);

  float winW = ImGui::GetWindowWidth();
  float winH = ImGui::GetWindowHeight();
  
  float panelW = 400.0f;
  float panelH = 340.0f;
  ImGui::SetCursorPos(ImVec2((winW - panelW) * 0.5f, (winH - panelH) * 0.5f));

  ImGui::BeginChild("PausePanel", ImVec2(panelW, panelH), ImGuiChildFlags_Border);
  
  ImGui::Spacing();
  float titleW = ImGui::CalcTextSize("Paused").x;
  ImGui::SetCursorPosX((panelW - titleW) * 0.5f);
  ImGui::Text("Paused");
  ImGui::Separator();
  ImGui::Spacing(); ImGui::Spacing();

  float btnW = 240.0f;
  float btnH = 45.0f;
  float btnStartX = (panelW - btnW) * 0.5f;

  ImGui::SetCursorPosX(btnStartX);
  if (ImGui::Button("Resume", ImVec2(btnW, btnH))) {
    handleAction("resume-game");
  }
  
  ImGui::Spacing();
  ImGui::SetCursorPosX(btnStartX);
  if (ImGui::Button("Options", ImVec2(btnW, btnH))) {
    showOptions();
  }
  
  ImGui::Spacing();
  ImGui::SetCursorPosX(btnStartX);
  if (ImGui::Button("Quit to Title", ImVec2(btnW, btnH))) {
    handleAction("quit-to-title");
  }
  ImGui::EndChild();

  ImGui::End();
}

void UIManager::renderFpsOverlay() {
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 10.0f),
                          ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.35f);
  ImGui::Begin("FpsOverlay", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "FPS: %.0f", m_currentFps);
  ImGui::End();
}

void UIManager::renderOptionsMenu() {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("OptionsMenu", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoSavedSettings);

  float winW = ImGui::GetWindowWidth();
  float winH = ImGui::GetWindowHeight();
  
  float panelW = 400.0f;
  float panelH = 280.0f;
  ImGui::SetCursorPos(ImVec2((winW - panelW) * 0.5f, (winH - panelH) * 0.5f));

  ImGui::BeginChild("OptionsPanel", ImVec2(panelW, panelH), ImGuiChildFlags_Border);
  
  ImGui::Spacing();
  float titleW = ImGui::CalcTextSize("Options").x;
  ImGui::SetCursorPosX((panelW - titleW) * 0.5f);
  ImGui::Text("Options");
  ImGui::Separator();
  ImGui::Spacing(); ImGui::Spacing();

  ImGui::SetCursorPosX(40);
  ImGui::Text("Render Distance: %d", m_renderDistance);
  ImGui::SetCursorPosX(40);
  ImGui::PushItemWidth(320);
  ImGui::SliderInt("##rd", &m_renderDistance, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
  ImGui::PopItemWidth();

  ImGui::Spacing(); ImGui::Spacing();
  float btnW = 200.0f;
  ImGui::SetCursorPosX((panelW - btnW) * 0.5f);
  if (ImGui::Button("Back", ImVec2(btnW, 40))) {
    if (m_callbacks.onRenderDistanceChanged) m_callbacks.onRenderDistanceChanged();
    m_state = m_previousState;
  }
  ImGui::EndChild();

  ImGui::End();
}

void UIManager::renderHotbar(int32_t selectedSlot) {
  float winW = ImGui::GetIO().DisplaySize.x;
  float slotSize = 48.0f;
  float totalW = 9 * slotSize + 8 * 6.0f; // 9 slots + gaps
  float startX = (winW - totalW) * 0.5f;
  float y = ImGui::GetIO().DisplaySize.y - 72.0f;

  ImGui::SetNextWindowPos(ImVec2(startX, y));
  ImGui::SetNextWindowSize(ImVec2(totalW, slotSize + 8));
  ImGui::Begin("Hotbar", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);

  for (int32_t i = 0; i < 9; ++i) {
    if (i > 0) ImGui::SameLine(0, 6.0f);
    ImGui::PushID(i);
    ImVec4 color = (i == selectedSlot) ? ImVec4(1.0f, 0.85f, 0.35f, 0.8f)
                                       : ImVec4(0.3f, 0.3f, 0.3f, 0.6f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::Button(" ", ImVec2(slotSize, slotSize));
    ImGui::PopStyleColor();
    ImGui::PopID();
  }

  ImGui::End();
}

void UIManager::renderInventory(bool open) {
  if (!open) return;

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("InventoryOverlay", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);

  float slotSize = 46.0f;
  float totalW = 9 * slotSize + 8 * 8.0f;
  ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - totalW) * 0.5f,
                              ImGui::GetWindowHeight() * 0.3f));

  ImGui::BeginChild("InvPanel", ImVec2(totalW + 16, 5 * slotSize + 5 * 8 + 16),
                    ImGuiChildFlags_Border);
  for (int32_t row = 0; row < 5; ++row) {
    for (int32_t col = 0; col < 9; ++col) {
      if (col > 0) ImGui::SameLine(0, 8.0f);
      ImGui::PushID(row * 9 + col);
      ImGui::Button(" ", ImVec2(slotSize, slotSize));
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

} // namespace terrain
