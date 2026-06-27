#include "InputCallbackBridge.hpp"
#include "engine/core/InputState.hpp"
#include "backends/imgui_impl_glfw.h"
#include <GLFW/glfw3.h>

namespace terrain {
namespace {

struct CallbackContext {
  InputState* input = nullptr;
  double lastX = 0.0;
  double lastY = 0.0;
  bool firstMouse = true;
};

CallbackContext* g_inputContext = nullptr;
CallbackContext g_inputContextStorage{};
GLFWwindow* g_inputWindow = nullptr;

// @see notes/imGui-window-user-pointer-conflict.md
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
      ctx->input->addMouseDelta(
        static_cast<float>(x - ctx->lastX),
        static_cast<float>(y - ctx->lastY));
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

void installCallbacks(GLFWwindow* window) {
  glfwSetKeyCallback(window, onKey);
  glfwSetMouseButtonCallback(window, onMouseButton);
  glfwSetCursorPosCallback(window, onCursor);
  glfwSetScrollCallback(window, onScroll);
  glfwSetCharCallback(window, onChar);
  glfwSetWindowFocusCallback(window, onWindowFocus);
  glfwSetCursorEnterCallback(window, onCursorEnter);
}

} // namespace

void setupInputCallbacks(GLFWwindow* window, InputState& input) {
  g_inputContextStorage.input = &input;
  g_inputContextStorage.lastX = 0.0;
  g_inputContextStorage.lastY = 0.0;
  g_inputContextStorage.firstMouse = true;
  g_inputContext = &g_inputContextStorage;
  g_inputWindow = window;
  installCallbacks(window);
}

void clearInputCallbacks() {
  if (g_inputWindow) {
    glfwSetKeyCallback(g_inputWindow, nullptr);
    glfwSetMouseButtonCallback(g_inputWindow, nullptr);
    glfwSetCursorPosCallback(g_inputWindow, nullptr);
    glfwSetScrollCallback(g_inputWindow, nullptr);
    glfwSetCharCallback(g_inputWindow, nullptr);
    glfwSetWindowFocusCallback(g_inputWindow, nullptr);
    glfwSetCursorEnterCallback(g_inputWindow, nullptr);
    g_inputWindow = nullptr;
  }
  g_inputContext = nullptr;
  g_inputContextStorage.input = nullptr;
  g_inputContextStorage.firstMouse = true;
}

} // namespace terrain
