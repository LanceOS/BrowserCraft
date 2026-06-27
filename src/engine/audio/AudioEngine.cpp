#include "AudioEngine.hpp"
#include <algorithm>
#include <cmath>

namespace terrain::audio {

// ---- AudioRegistry ----

AudioRegistry::AudioRegistry() = default;

void AudioRegistry::seedBuiltinSounds(int32_t sampleRate) {
  register_(SoundId::STONE_BREAK, createNoiseBuffer(sampleRate, 0.18f, 2.6f, 0.15f));
  register_(SoundId::STONE_STEP,  createNoiseBuffer(sampleRate, 0.07f, 3.4f, 0.05f));
  register_(SoundId::GRASS_BREAK, createNoiseBuffer(sampleRate, 0.16f, 2.2f, 0.32f));
  register_(SoundId::GRASS_STEP,  createNoiseBuffer(sampleRate, 0.06f, 2.8f, 0.18f));
  register_(SoundId::WOOD_BREAK,  createNoiseBuffer(sampleRate, 0.14f, 2.4f, 0.48f));
  register_(SoundId::ZOMBIE_GROAN, createToneBuffer(sampleRate, 0.65f, 112.0f, 4.0f));
  register_(SoundId::ZOMBIE_HURT,  createToneBuffer(sampleRate, 0.24f, 176.0f, 8.0f));
}

auto AudioRegistry::get(SoundId id) const -> const AudioBuffer* {
  auto it = m_buffers.find(static_cast<int32_t>(id));
  return it != m_buffers.end() ? &it->second : nullptr;
}

void AudioRegistry::register_(SoundId id, AudioBuffer buffer) {
  m_buffers[static_cast<int32_t>(id)] = std::move(buffer);
}

auto AudioRegistry::createNoiseBuffer(int32_t sampleRate, float duration,
                                       float decayPower, float toneMix) -> AudioBuffer {
  int32_t frameCount = std::max(1, static_cast<int32_t>(sampleRate * duration));
  AudioBuffer buf;
  buf.sampleRate = sampleRate;
  buf.samples.resize(frameCount);

  std::mt19937 rng(12345);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  for (int32_t i = 0; i < frameCount; ++i) {
    float t = static_cast<float>(i) / frameCount;
    float envelope = std::pow(1.0f - t, decayPower);
    float noise = dist(rng) * envelope;
    float tone = std::sin(t * 3.14159265f * (8.0f + toneMix * 12.0f)) * envelope * toneMix;
    float sample = noise * (1.0f - toneMix) + tone * 0.35f;
    buf.samples[i] = std::clamp(sample, -1.0f, 1.0f);
  }
  return buf;
}

auto AudioRegistry::createToneBuffer(int32_t sampleRate, float duration,
                                      float baseFreq, float wobble) -> AudioBuffer {
  int32_t frameCount = std::max(1, static_cast<int32_t>(sampleRate * duration));
  AudioBuffer buf;
  buf.sampleRate = sampleRate;
  buf.samples.resize(frameCount);

  for (int32_t i = 0; i < frameCount; ++i) {
    float t = static_cast<float>(i) / sampleRate;
    float life = static_cast<float>(i) / frameCount;
    float envelope = std::pow(1.0f - life, 1.8f);
    float freq = baseFreq + std::sin(t * wobble) * (baseFreq * 0.08f);
    float fundamental = std::sin(t * 3.14159265f * 2.0f * freq);
    float overtone = std::sin(t * 3.14159265f * 2.0f * freq * 1.5f) * 0.35f;
    float sample = (fundamental + overtone) * envelope * 0.45f;
    buf.samples[i] = std::clamp(sample, -1.0f, 1.0f);
  }
  return buf;
}

// ---- AudioEngine ----

AudioEngine::AudioEngine() {
  // Stub initialization — in production, this would initialize miniaudio.
  m_initialized = true;
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::playOneShot(const AudioBuffer& buffer, float x, float y, float z,
                               float volume, float pitch) {
  ActiveSound snd;
  snd.samples = buffer.samples;
  snd.sampleRate = buffer.sampleRate;
  snd.position = 0;
  snd.volume = volume * m_masterVolume;
  snd.pitch = pitch;
  snd.x = x; snd.y = y; snd.z = z;
  m_activeSounds.push_back(std::move(snd));
}

void AudioEngine::setListenerPosition(float, float, float) {}
void AudioEngine::setListenerOrientation(float, float, float, float, float, float) {}
void AudioEngine::setMasterVolume(float vol) { m_masterVolume = std::clamp(vol, 0.0f, 1.0f); }
void AudioEngine::update(float) {
  // Clear finished sounds
  m_activeSounds.clear();
}

} // namespace terrain::audio
