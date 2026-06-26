#pragma once

#include <GLFW/glfw3.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace terrain {

/// Keyboard and mouse input state, updated each frame by GLFW callbacks.
class InputState {
public:
  static constexpr int32_t KEY_W = 0;
  static constexpr int32_t KEY_A = 1;
  static constexpr int32_t KEY_S = 2;
  static constexpr int32_t KEY_D = 3;
  static constexpr int32_t KEY_SPACE = 4;
  static constexpr int32_t KEY_SHIFT = 5;
  static constexpr int32_t KEY_CTRL = 6;
  static constexpr int32_t KEY_ESCAPE = 7;
  static constexpr int32_t KEY_E = 8;
  static constexpr int32_t KEY_1 = 9;
  static constexpr int32_t KEY_2 = 10;
  static constexpr int32_t KEY_3 = 11;
  static constexpr int32_t KEY_4 = 12;
  static constexpr int32_t KEY_5 = 13;
  static constexpr int32_t KEY_6 = 14;
  static constexpr int32_t KEY_7 = 15;
  static constexpr int32_t KEY_8 = 16;
  static constexpr int32_t KEY_9 = 17;
  static constexpr int32_t KEY_COUNT = 32;

  InputState();

  /// Set a key state (called from GLFW callback).
  void setKey(int32_t idx, bool down);

  /// Set a key by GLFW key code.
  void setKeyByCode(int32_t glfwKey, bool down);

  /// Set mouse button state.
  void setMouseButton(int32_t button, bool down);

  /// Add mouse delta (from cursor movement).
  void addMouseDelta(float dx, float dy);

  /// Call at end of frame to clear transient state.
  void clearFrameState();

  /// Clear all input (e.g., on menu/pause).
  void clearAll();

  [[nodiscard]] auto isPressed(int32_t idx) const -> bool { return m_keys[idx] == 1; }
  [[nodiscard]] auto isHeld(int32_t idx) const -> bool { return m_keys[idx] > 0; }
  [[nodiscard]] auto isMousePressed(int32_t btn) const -> bool { return m_mouseButtons[btn] == 1; }
  [[nodiscard]] auto isMouseHeld(int32_t btn) const -> bool { return m_mouseButtons[btn] > 0; }

  [[nodiscard]] auto mouseDX() const -> float { return m_mouseDelta[0]; }
  [[nodiscard]] auto mouseDY() const -> float { return m_mouseDelta[1]; }
  [[nodiscard]] auto mouseButton(int32_t btn) const -> uint8_t { return m_mouseButtons[btn]; }

  bool pointerLocked = false;

private:
  std::array<uint8_t, KEY_COUNT> m_keys{};
  std::array<float, 2> m_mouseDelta{};
  std::array<uint8_t, 3> m_mouseButtons{};
};

} // namespace terrain
