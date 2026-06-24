#include "GameLoop.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace voxel {

GameLoop::GameLoop(float targetFps, UpdateFn update, RenderFn render)
  : m_fixedStep(1.0f / targetFps),
    m_update(std::move(update)),
    m_render(std::move(render))
{}

void GameLoop::run() {
  m_running = true;
  m_lastTime = static_cast<float>(glfwGetTime());
  m_accumulator = 0.0f;

  while (m_running) {
    float now = static_cast<float>(glfwGetTime());
    float frameDt = now - m_lastTime;
    m_lastTime = now;

    // Clamp to avoid spiral of death
    frameDt = std::min(frameDt, 0.1f);

    m_accumulator += frameDt;
    while (m_accumulator >= m_fixedStep) {
      m_update(m_fixedStep);
      m_accumulator -= m_fixedStep;
    }

    float alpha = m_accumulator / m_fixedStep;
    m_render(alpha, now);
  }
}

void GameLoop::stop() {
  m_running = false;
}

} // namespace voxel
