#include <GLFW/glfw3.h>
#include <iostream>
#include "engine/render/gl_core.hpp"
#include "engine/core/ConfigDefaults.hpp"
#include "engine/core/InputCallbackBridge.hpp"
#include "game/Game.hpp"

auto main() -> int {
  std::cout << "=== Terrain Engine (C++) ===\n";

  if (!glfwInit()) { std::cerr << "Failed to init GLFW\n"; return 1; }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "Terrain Engine", nullptr, nullptr);
  if (!window) { std::cerr << "Failed to create window\n"; glfwTerminate(); return 1; }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  terrain::gl::loadGLFunctions();
  std::cout << "OpenGL loaded\n";

  {
    auto config = terrain::makeDefaultGameConfig();
    terrain::Game game(window, config, {.initialState = terrain::GameState::MainMenu});
    auto& input = game.input();

    // @see notes/imGui-manual-callback-forwarding.md
    // ImGui and game input are both wired through explicit callbacks after ImGui context creation.
    terrain::setupInputCallbacks(window, input);

    game.run();
    terrain::clearInputCallbacks();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  std::cout << "Engine shut down cleanly.\n";
  return 0;
}
