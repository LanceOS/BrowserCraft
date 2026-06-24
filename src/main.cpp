#include <GLFW/glfw3.h>
#include <iostream>
#include "engine/render/gl_core.hpp"
#include "engine/core/Config.hpp"
#include "engine/core/InputState.hpp"
#include "game/Game.hpp"

namespace {
struct CallbackContext {
  voxel::InputState* input = nullptr;
  double lastX = 0.0;
  double lastY = 0.0;
  bool firstMouse = true;
};

  voxel::GameConfig makeConfig() {
    voxel::GameConfig cfg{};
    cfg.chunkSize = 16; cfg.worldHeight = 256; cfg.renderDistance = 4;
    cfg.worldSeed = 42; cfg.maxVertsPerChunk = 50000;
    cfg.maxIndicesPerChunk = 100000; cfg.vertexStrideFloats = 10;
    cfg.textureArrayLayers = 64;
    return cfg;
  }

  void onKey(GLFWwindow* window, int key, int, int action, int) {
    auto* ctx = static_cast<CallbackContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->input) return;
    ctx->input->setKeyByCode(key, action != GLFW_RELEASE);
  }

  void onMouseButton(GLFWwindow* window, int button, int action, int) {
    auto* ctx = static_cast<CallbackContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->input) return;
    ctx->input->setMouseButton(button, action == GLFW_PRESS);
  }

  void onCursor(GLFWwindow* window, double x, double y) {
    auto* ctx = static_cast<CallbackContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->input) return;

    if (ctx->firstMouse) {
      ctx->lastX = x;
      ctx->lastY = y;
      ctx->firstMouse = false;
      return;
    }

    ctx->input->addMouseDelta(static_cast<float>(x - ctx->lastX), static_cast<float>(y - ctx->lastY));
    ctx->lastX = x;
    ctx->lastY = y;
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
  CallbackContext ctx{&input, 0.0, 0.0, true};
  glfwSetWindowUserPointer(window, &ctx);

  // Input callbacks
  glfwSetKeyCallback(window, onKey);
  glfwSetMouseButtonCallback(window, onMouseButton);
  glfwSetCursorPosCallback(window, onCursor);

  game.run();

  glfwDestroyWindow(window);
  glfwTerminate();
  std::cout << "Engine shut down cleanly.\n";
  return 0;
}
