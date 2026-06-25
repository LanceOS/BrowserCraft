#include <GLFW/glfw3.h>
#include <iostream>
#include "engine/render/gl_core.hpp"
#include "engine/core/Config.hpp"
#include "engine/core/InputState.hpp"
#include "game/Game.hpp"
#include "backends/imgui_impl_glfw.h"

namespace {
struct CallbackContext {
  voxel::InputState* input = nullptr;
  double lastX = 0.0;
  double lastY = 0.0;
  bool firstMouse = true;
};

// @see notes/imGui-window-user-pointer-conflict.md
CallbackContext* g_inputContext = nullptr;

  voxel::GameConfig makeConfig() {
    voxel::GameConfig cfg{};
    cfg.chunkSize = 16; cfg.worldHeight = 256; cfg.renderDistance = 4;
    cfg.maxVertsPerChunk = 50000;
    cfg.maxIndicesPerChunk = 100000; cfg.vertexStrideFloats = 10;
    cfg.textureArrayLayers = 64;
    return cfg;
  }

  void onKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* ctx = g_inputContext;
    if (ctx && ctx->input) ctx->input->setKeyByCode(key, action != GLFW_RELEASE);
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
  }

  void onMouseButton(GLFWwindow* window, int button, int action, int mods) {
    auto* ctx = g_inputContext;
    if (ctx && ctx->input) ctx->input->setMouseButton(button, action == GLFW_PRESS);
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
  }

  void onCursor(GLFWwindow* window, double x, double y) {
    auto* ctx = g_inputContext;
    if (ctx && ctx->input) {
      if (ctx->firstMouse) {
        ctx->lastX = x;
        ctx->lastY = y;
        ctx->firstMouse = false;
      } else {
        ctx->input->addMouseDelta(static_cast<float>(x - ctx->lastX), static_cast<float>(y - ctx->lastY));
      }

      ctx->lastX = x;
      ctx->lastY = y;
    }

    ImGui_ImplGlfw_CursorPosCallback(window, x, y);
  }

  void onScroll(GLFWwindow* window, double xOffset, double yOffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
  }

  void onChar(GLFWwindow* window, unsigned int codepoint) {
    ImGui_ImplGlfw_CharCallback(window, codepoint);
  }

  void onWindowFocus(GLFWwindow* window, int focused) {
    if (focused && g_inputContext) g_inputContext->firstMouse = true;
    ImGui_ImplGlfw_WindowFocusCallback(window, focused);
  }

  void onCursorEnter(GLFWwindow* window, int entered) {
    ImGui_ImplGlfw_CursorEnterCallback(window, entered);
  }

  void setupInputCallbacks(GLFWwindow* window) {
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursor);
    glfwSetScrollCallback(window, onScroll);
    glfwSetCharCallback(window, onChar);
    glfwSetWindowFocusCallback(window, onWindowFocus);
    glfwSetCursorEnterCallback(window, onCursorEnter);
  }
}

auto main() -> int {
  std::cout << "=== Voxel Engine (C++) ===\n";

  if (!glfwInit()) { std::cerr << "Failed to init GLFW\n"; return 1; }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "Voxel Engine", nullptr, nullptr);
  if (!window) { std::cerr << "Failed to create window\n"; glfwTerminate(); return 1; }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  voxel::gl::loadGLFunctions();
  std::cout << "OpenGL loaded\n";

  // Create and run game
  auto config = makeConfig();
  voxel::Game game(window, config, {.initialState = voxel::GameState::MainMenu});
  auto& input = game.input();

  CallbackContext ctx;
  ctx.input = &input;
  g_inputContext = &ctx;

  // @see notes/imGui-manual-callback-forwarding.md
  // ImGui and game input are both wired through explicit callbacks after ImGui context creation.
  setupInputCallbacks(window);

  game.run();
  g_inputContext = nullptr;

  glfwDestroyWindow(window);
  glfwTerminate();
  std::cout << "Engine shut down cleanly.\n";
  return 0;
}
