#pragma once

#include <cmath>

namespace voxel {
namespace daynight {

constexpr float kDefaultDayLengthSeconds = 3600.0f;
constexpr float kDefaultNightLightMin = 0.20f;
constexpr float kMiddayTimeSeconds = kDefaultDayLengthSeconds * 0.5f;

[[nodiscard]] auto computeSunAngularSpeed(float dayLengthSeconds = kDefaultDayLengthSeconds) -> float;
[[nodiscard]] auto computeSunAngle(float timeSeconds, float dayLengthSeconds = kDefaultDayLengthSeconds) -> float;
[[nodiscard]] auto computeDaylightFactor(float timeSeconds,
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
  [[nodiscard]] auto dayLength() const -> float;

private:
  static auto wrapTime(float timeSeconds, float dayLengthSeconds) -> float;

  float m_dayLengthSeconds;
  float m_nightLightMin;
  float m_timeSeconds = 0.0f;
};

} // namespace daynight
} // namespace voxel
