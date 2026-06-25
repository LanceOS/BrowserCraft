#include "DayNightCycle.hpp"

namespace voxel {
namespace daynight {

namespace {
constexpr float kTwoPi = 6.283185307179586476925286766559f;
constexpr float kSunOffset = -1.57079632679f;
} // namespace

auto computeSunAngularSpeed(float dayLengthSeconds) -> float {
  if (dayLengthSeconds <= 0.0f) {
    return 0.0f;
  }
  return kTwoPi / dayLengthSeconds;
}

auto computeSunAngle(float timeSeconds, float dayLengthSeconds) -> float {
  return timeSeconds * computeSunAngularSpeed(dayLengthSeconds);
}

auto computeDaylightFactor(float timeSeconds, float dayLengthSeconds, float nightLightMin) -> float {
  if (dayLengthSeconds <= 0.0f) return nightLightMin;
  float angle = computeSunAngle(timeSeconds, dayLengthSeconds) + kSunOffset;
  float daylight = std::sin(angle) * 0.5f + 0.5f;
  if (daylight < 0.0f) daylight = 0.0f;
  if (daylight > 1.0f) daylight = 1.0f;
  return nightLightMin + (1.0f - nightLightMin) * daylight;
}

DayNightCycle::DayNightCycle(float dayLengthSeconds, float nightLightMin)
  : m_dayLengthSeconds(dayLengthSeconds), m_nightLightMin(nightLightMin), m_timeSeconds(0.0f) {}

void DayNightCycle::setTime(float timeSeconds) {
  m_timeSeconds = wrapTime(timeSeconds, m_dayLengthSeconds);
}

void DayNightCycle::advance(float dt) {
  m_timeSeconds = wrapTime(m_timeSeconds + dt, m_dayLengthSeconds);
}

auto DayNightCycle::time() const -> float {
  return m_timeSeconds;
}

auto DayNightCycle::daylight() const -> float {
  return computeDaylightFactor(m_timeSeconds, m_dayLengthSeconds, m_nightLightMin);
}

auto DayNightCycle::sunAngle() const -> float {
  return computeSunAngle(m_timeSeconds, m_dayLengthSeconds);
}

auto DayNightCycle::dayLength() const -> float {
  return m_dayLengthSeconds;
}

auto DayNightCycle::wrapTime(float timeSeconds, float dayLengthSeconds) -> float {
  if (dayLengthSeconds <= 0.0f) return 0.0f;

  float wrapped = std::fmod(timeSeconds, dayLengthSeconds);
  if (wrapped < 0.0f) wrapped += dayLengthSeconds;
  return wrapped;
}

} // namespace daynight
} // namespace voxel
