#include "InputState.hpp"
#include <cstring>

namespace terrain {

InputState::InputState() {
  m_keys.fill(0);
  m_mouseDelta.fill(0);
  m_mouseButtons.fill(0);
}

void InputState::setKey(int32_t idx, bool down) {
  if (idx < 0 || idx >= KEY_COUNT) return;
  m_keys[idx] = down ? (m_keys[idx] > 0 ? 2 : 1) : 0;
}

void InputState::setKeyByCode(int32_t glfwKey, bool down) {
  static const std::unordered_map<int32_t, int32_t> map = {
    {GLFW_KEY_W, KEY_W}, {GLFW_KEY_A, KEY_A}, {GLFW_KEY_S, KEY_S}, {GLFW_KEY_D, KEY_D},
    {GLFW_KEY_SPACE, KEY_SPACE}, {GLFW_KEY_LEFT_SHIFT, KEY_SHIFT}, {GLFW_KEY_RIGHT_SHIFT, KEY_SHIFT},
    {GLFW_KEY_LEFT_CONTROL, KEY_CTRL}, {GLFW_KEY_RIGHT_CONTROL, KEY_CTRL},
    {GLFW_KEY_ESCAPE, KEY_ESCAPE}, {GLFW_KEY_E, KEY_E},
    {GLFW_KEY_1, KEY_1}, {GLFW_KEY_2, KEY_2}, {GLFW_KEY_3, KEY_3}, {GLFW_KEY_4, KEY_4},
    {GLFW_KEY_5, KEY_5}, {GLFW_KEY_6, KEY_6}, {GLFW_KEY_7, KEY_7}, {GLFW_KEY_8, KEY_8},
    {GLFW_KEY_9, KEY_9},
  };
  auto it = map.find(glfwKey);
  if (it != map.end()) setKey(it->second, down);
}

void InputState::setMouseButton(int32_t button, bool down) {
  if (button < 0 || button >= static_cast<int32_t>(m_mouseButtons.size())) return;
  m_mouseButtons[button] = down ? (m_mouseButtons[button] > 0 ? 2 : 1) : 0;
}

void InputState::addMouseDelta(float dx, float dy) {
  m_mouseDelta[0] += dx;
  m_mouseDelta[1] += dy;
}

void InputState::clearFrameState() {
  for (auto& k : m_keys) {
    if (k == 1) k = 2; // pressed -> held
  }
  for (auto& button : m_mouseButtons) {
    if (button == 1) button = 2; // pressed -> held
  }
  m_mouseDelta[0] = 0;
  m_mouseDelta[1] = 0;
}

void InputState::clearAll() {
  m_keys.fill(0);
  m_mouseDelta.fill(0);
  m_mouseButtons.fill(0);
}

} // namespace terrain
