#include <GLFW/glfw3.h>
#include <iostream>
#include "engine/render/gl_core.hpp"
#include "engine/core/ConfigDefaults.hpp"
#include "engine/core/InputCallbackBridge.hpp"
#include "game/Game.hpp"

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
  auto config = voxel::makeDefaultGameConfig();
  voxel::Game game(window, config, {.initialState = voxel::GameState::MainMenu});
  auto& input = game.input();

  // @see notes/imGui-manual-callback-forwarding.md
  // ImGui and game input are both wired through explicit callbacks after ImGui context creation.
  voxel::setupInputCallbacks(window, input);

  game.run();
  voxel::clearInputCallbacks();

  glfwDestroyWindow(window);
  glfwTerminate();
  std::cout << "Engine shut down cleanly.\n";
  return 0;
}
