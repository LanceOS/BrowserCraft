#include "UIManager.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cctype>

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
} // namespace

namespace voxel {

UIManager::UIManager(GLFWwindow* window, Callbacks callbacks)
  : m_window(window), m_callbacks(std::move(callbacks)) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr; // don't save layout
  std::strncpy(m_slotId.data(), "default", m_slotId.size() - 1);
  m_slotId[m_slotId.size() - 1] = '\0';

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 460");
  m_initialized = true;
}

UIManager::~UIManager() {
  if (m_initialized) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
}

void UIManager::beginFrame() {
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
      renderHotbar(-1);
      renderInventory(m_inventoryOpen);
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
void UIManager::showOptions() { m_state = UIState::Options; }
void UIManager::clearUI() { m_state = UIState::InGame; }

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
  ImGui::SetCursorPos(ImVec2(winW * 0.5f - 200.0f, winH * 0.35f));

  ImGui::BeginChild("MenuPanel", ImVec2(400, 300), ImGuiChildFlags_Border);
  ImGui::SetCursorPosX(120);
  ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "VOXEL ENGINE");

  ImGui::Spacing(); ImGui::Spacing();

  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Start Game", ImVec2(200, 40))) {
    m_state = UIState::GameModeMenu;
  }
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Options", ImVec2(200, 40))) {
    showOptions();
  }
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Quit", ImVec2(200, 40))) {
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
  ImGui::SetCursorPos(ImVec2(winW * 0.5f - 200.0f, winH * 0.35f));

  ImGui::BeginChild("ModePanel", ImVec2(420, 320), ImGuiChildFlags_Border);
  ImGui::SetCursorPosX(155);
  ImGui::Text("Select Mode");
  ImGui::Spacing();

  ImGui::SetCursorPosX(95);
  ImGui::RadioButton("Survival##menu_mode", &m_gameModeIndex, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Creative##menu_mode", &m_gameModeIndex, 1);

  ImGui::Spacing();
  ImGui::SetCursorPosX(90);
  ImGui::Text("World Slot");
  ImGui::SetCursorPosX(80);
  ImGui::InputText("##world_slot", m_slotId.data(), m_slotId.size());

  const std::string slotId = sanitizeSlotId(std::string(m_slotId.data()));
  const bool hasValidSlot = !slotId.empty();
  const GameMode mode = m_gameModeIndex == 1 ? GameMode::Creative : GameMode::Survival;

  ImGui::Spacing();
  if (!hasValidSlot) {
    ImGui::SetCursorPosX(50);
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "World slot cannot be blank");
  } else if (slotId != std::string(m_slotId.data())) {
    ImGui::SetCursorPosX(22);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Invalid characters were replaced with '_'");
  }

  ImGui::Spacing();
  ImGui::SetCursorPosX(100);
  if (!hasValidSlot) ImGui::BeginDisabled();
  if (ImGui::Button("Load World", ImVec2(200, 40))) {
    if (m_callbacks.onStartWorld) m_callbacks.onStartWorld(mode, slotId.empty() ? "default" : slotId, false);
  }
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Start New World", ImVec2(200, 40))) {
    if (m_callbacks.onStartWorld) m_callbacks.onStartWorld(mode, slotId.empty() ? "default" : slotId, true);
  }
  if (!hasValidSlot) ImGui::EndDisabled();
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Back", ImVec2(200, 40))) {
    m_state = UIState::MainMenu;
  }
  ImGui::EndChild();

  ImGui::End();
}

void UIManager::renderPauseMenu() {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("PauseMenu", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);

  float winW = ImGui::GetWindowWidth();
  float winH = ImGui::GetWindowHeight();
  ImGui::SetCursorPos(ImVec2(winW * 0.5f - 200.0f, winH * 0.35f));

  ImGui::BeginChild("PausePanel", ImVec2(400, 250), ImGuiChildFlags_Border);
  ImGui::SetCursorPosX(150);
  ImGui::Text("Paused");
  ImGui::Spacing();

  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Resume", ImVec2(200, 40))) {
    handleAction("resume-game");
  }
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Options", ImVec2(200, 40))) {
    showOptions();
  }
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Quit to Title", ImVec2(200, 40))) {
    handleAction("quit-to-title");
  }
  ImGui::EndChild();

  ImGui::End();
}

void UIManager::renderOptionsMenu() {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("OptionsMenu", nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);

  float winW = ImGui::GetWindowWidth();
  float winH = ImGui::GetWindowHeight();
  ImGui::SetCursorPos(ImVec2(winW * 0.5f - 200.0f, winH * 0.3f));

  ImGui::BeginChild("OptionsPanel", ImVec2(400, 280), ImGuiChildFlags_Border);
  ImGui::SetCursorPosX(150);
  ImGui::Text("Options");
  ImGui::Spacing();

  ImGui::SetCursorPosX(50);
  ImGui::Text("Render Distance: %d", m_renderDistance);
  ImGui::SetCursorPosX(50);
  ImGui::SliderInt("##rd", &m_renderDistance, 2, 32);

  ImGui::Spacing();
  ImGui::SetCursorPosX(100);
  if (ImGui::Button("Save", ImVec2(200, 40))) {
    if (m_callbacks.onRenderDistanceChanged) m_callbacks.onRenderDistanceChanged();
    if (m_state == UIState::Paused) showPauseMenu();
    else showMainMenu();
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

} // namespace voxel
