#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace voxel {
namespace daynight {

// 20 minutes real-time = 1 full day/night cycle
constexpr float kDefaultDayLengthSeconds = 1200.0f;
constexpr float kDefaultNightLightMin = 0.15f;
constexpr float kMiddayTimeSeconds = kDefaultDayLengthSeconds * 0.5f;
constexpr float kMidnightTimeSeconds = 0.0f;

/// Sun angle (radians) over the course of a day.
/// 0     = midnight (sun below)
/// π/2  = sunrise  (east horizon)
/// π    = noon     (overhead)
/// 3π/2 = sunset   (west horizon)
[[nodiscard]] auto computeSunAngle(float timeSeconds, float dayLengthSeconds = kDefaultDayLengthSeconds) -> float;

/// Daylight factor: 0.0 at night, 1.0 at noon, smooth sin transition.
[[nodiscard]] auto computeDaylightFactor(float timeSeconds,
                                       float dayLengthSeconds = kDefaultDayLengthSeconds,
                                       float nightLightMin = kDefaultNightLightMin) -> float;

/// Compute the normalized direction vector FROM a surface TOWARD the sun.
/// Returned as (sunX, sunY, sunZ).
[[nodiscard]] auto computeSunDirection(float timeSeconds, float dayLengthSeconds = kDefaultDayLengthSeconds) -> glm::vec3;

/// Compute sun color (RGB). Warm at sunrise/sunset, white at noon, dim/dark at night.
[[nodiscard]] auto computeSunColor(float timeSeconds, float dayLengthSeconds = kDefaultDayLengthSeconds) -> glm::vec3;

/// Compute ambient sky intensity (0.0 = pitch black, 1.0 = full daylight ambient).
[[nodiscard]] auto computeAmbientIntensity(float timeSeconds,
                                          float dayLengthSeconds = kDefaultDayLengthSeconds,
                                          float nightLightMin = kDefaultNightLightMin) -> float;

class DayNightCycle {
public:
  explicit DayNightCycle(float dayLengthSeconds = kDefaultDayLengthSeconds,
                         float nightLightMin = kDefaultNightLightMin);

  void setTime(float timeSeconds);
  void advance(float dt);

  [[nodiscard]] auto time() const -> float;
  [[nodiscard]] auto daylight() const -> float;
  [[nodiscard]] auto sunAngle() const -> float;
  [[nodiscard]] auto sunDirection() const -> glm::vec3;
  [[nodiscard]] auto sunColor() const -> glm::vec3;
  [[nodiscard]] auto ambientIntensity() const -> float;
  [[nodiscard]] auto dayLength() const -> float;

private:
  static auto wrapTime(float timeSeconds, float dayLengthSeconds) -> float;

  float m_dayLengthSeconds;
  float m_nightLightMin;
  float m_timeSeconds = 0.0f;
};

} // namespace daynight
} // namespace voxel
