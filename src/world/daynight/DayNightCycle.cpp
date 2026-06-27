#include "DayNightCycle.hpp"
#include <algorithm>

// @deprecated Legacy terrain-world code retained during the render-only migration to triangle meshes.
namespace terrain {
namespace daynight {

namespace {
constexpr float kTwoPi = 6.283185307179586476925286766559f;
} // namespace

static auto computeAngle(float timeSeconds, float dayLengthSeconds) -> float {
  if (dayLengthSeconds <= 0.0f) return 0.0f;
  return (timeSeconds / dayLengthSeconds) * kTwoPi;
}

auto computeSunAngle(float timeSeconds, float dayLengthSeconds) -> float {
  return computeAngle(timeSeconds, dayLengthSeconds);
}

auto computeDaylightFactor(float timeSeconds, float dayLengthSeconds, float nightLightMin) -> float {
  if (dayLengthSeconds <= 0.0f) return nightLightMin;
  float angle = computeAngle(timeSeconds, dayLengthSeconds);
  // -cos(angle): -1 at angle=0 (midnight), +1 at angle=π (noon)
  float raw = -std::cos(angle);
  float daylight = raw * 0.5f + 0.5f; // map [-1,+1] → [0,1]
  daylight = std::clamp(daylight, 0.0f, 1.0f);
  return nightLightMin + (1.0f - nightLightMin) * daylight;
}

auto computeSunDirection(float timeSeconds, float dayLengthSeconds) -> glm::vec3 {
  if (dayLengthSeconds <= 0.0f) return glm::vec3(0.0f, 1.0f, 0.0f);
  float angle = computeAngle(timeSeconds, dayLengthSeconds);
  // Sun arc: east (X+) → overhead (Y+) → west (X-)
  // angle=0   (midnight): ( 0, -1, 0) — below
  // angle=π/2 (sunrise):  ( 1,  0, 0) — east horizon
  // angle=π   (noon):     ( 0,  1, 0) — overhead
  // angle=3π/2 (sunset):  (-1,  0, 0) — west horizon
  float sx = std::sin(angle);
  float sy = -std::cos(angle);
  float sz = 0.0f;
  return glm::normalize(glm::vec3(sx, sy, sz));
}

// @see notes/daynight-sun-tint.md
auto computeSunColor(float timeSeconds, float dayLengthSeconds) -> glm::vec3 {
  if (dayLengthSeconds <= 0.0f) return glm::vec3(1.0f);
  float angle = computeAngle(timeSeconds, dayLengthSeconds);
  float elevation = -std::cos(angle); // -1 below, +1 overhead

  // Below horizon → no sun contribution (dark/ambient only)
  if (elevation <= -0.1f) return glm::vec3(0.0f, 0.0f, 0.0f);

  // Sun color shifts to warm orange/red at low elevation (sunrise/sunset)
  float horizonFactor = std::max(0.0f, 1.0f - elevation * 2.0f); // 1 at horizon, 0 at 45° up
  horizonFactor = horizonFactor * horizonFactor; // sharper transition

  // Keep the noon sun close to white so it reads like a bright light source,
  // while still allowing a gentle warm tint near the horizon.
  glm::vec3 dayColor(1.0f, 0.985f, 0.95f);      // neutral white at noon
  glm::vec3 duskColor(1.0f, 0.82f, 0.58f);      // soft gold at the horizon

  // Blend between day and dusk/sunset colors based on elevation
  glm::vec3 color = glm::mix(dayColor, duskColor, horizonFactor);

  // Dim the sun intensity near the horizon (atmospheric extinction)
  float intensity = std::max(0.0f, elevation * 1.5f + 0.5f);
  intensity = std::clamp(intensity, 0.0f, 1.0f);

  return color * intensity;
}

auto computeAmbientIntensity(float timeSeconds, float dayLengthSeconds, float nightLightMin) -> float {
  float daylight = computeDaylightFactor(timeSeconds, dayLengthSeconds, 0.0f);
  // Ambient is always a bit above the pure night minimum for visibility
  float minAmbient = nightLightMin;
  float maxAmbient = 0.45f;
  return minAmbient + (maxAmbient - minAmbient) * daylight;
}

// -----------------------------------------------------------------------
// DayNightCycle class
// -----------------------------------------------------------------------

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

auto DayNightCycle::sunDirection() const -> glm::vec3 {
  return computeSunDirection(m_timeSeconds, m_dayLengthSeconds);
}

auto DayNightCycle::sunColor() const -> glm::vec3 {
  return computeSunColor(m_timeSeconds, m_dayLengthSeconds);
}

auto DayNightCycle::ambientIntensity() const -> float {
  return computeAmbientIntensity(m_timeSeconds, m_dayLengthSeconds, m_nightLightMin);
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
} // namespace terrain
