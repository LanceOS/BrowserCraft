#include <GLFW/glfw3.h>
#include <iostream>
#include "engine/render/gl_core.hpp"
#include "engine/core/Config.hpp"
#include "engine/core/InputState.hpp"
#include "game/Game.hpp"

namespace {
  voxel::GameConfig makeConfig() {
    voxel::GameConfig cfg{};
    cfg.chunkSize = 16; cfg.worldHeight = 256; cfg.renderDistance = 4;
    cfg.worldSeed = 42; cfg.maxVertsPerChunk = 50000;
    cfg.maxIndicesPerChunk = 100000; cfg.vertexStrideFloats = 10;
    cfg.textureArrayLayers = 64;
    return cfg;
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

  // Input callbacks
  glfwSetKeyCallback(window, [&input](GLFWwindow*, int key, int, int action, int) {
    input.setKeyByCode(key, action != GLFW_RELEASE);
  });
  glfwSetMouseButtonCallback(window, [&input](GLFWwindow*, int btn, int act, int) {
    input.setMouseButton(btn, act == GLFW_PRESS);
  });
  glfwSetCursorPosCallback(window, [&input](GLFWwindow*, double x, double y) {
    static double lx=0,ly=0; static bool first=true;
    if (first) { lx = x; ly = y; first = false; }
    input.addMouseDelta(float(x-lx), float(y-ly));
    lx=x; ly=y;
  });

  game.run();

  glfwDestroyWindow(window);
  glfwTerminate();
  std::cout << "Engine shut down cleanly.\n";
  return 0;
}
