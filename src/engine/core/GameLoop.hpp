#pragma once

#include <functional>
#include <chrono>

namespace terrain {

/// Fixed-timestep game loop with variable rendering.
/// Calls update(fixedDt) at a fixed rate, then render(alpha, timeSeconds) each frame.
class GameLoop {
public:
  using UpdateFn = std::function<void(float)>;
  using RenderFn = std::function<void(float, float)>;

  GameLoop(float targetFps, UpdateFn update, RenderFn render);

  /// Start the loop. Blocks until stop() is called or window closes.
  void run();

  /// Request the loop to stop at the end of the current frame.
  void stop();

  [[nodiscard]] auto isRunning() const -> bool { return m_running; }

private:
  float m_fixedStep;
  float m_accumulator = 0.0f;
  float m_lastTime = 0.0f;
  bool m_running = false;

  UpdateFn m_update;
  RenderFn m_render;
};

} // namespace terrain
