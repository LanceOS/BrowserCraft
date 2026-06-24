#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <random>

namespace voxel::audio {

/// A simple PCM audio buffer (mono, float samples).
struct AudioBuffer {
  std::vector<float> samples;
  int32_t sampleRate = 44100;
  float duration() const { return static_cast<float>(samples.size()) / sampleRate; }
};

/// Procedural sound IDs matching the TS SoundId enum.
enum class SoundId : int32_t {
  STONE_BREAK,
  STONE_STEP,
  GRASS_BREAK,
  GRASS_STEP,
  WOOD_BREAK,
  ZOMBIE_GROAN,
  ZOMBIE_HURT,
  SKELETON_HURT,
  CLICK,
  AMBIENT_CAVE,
  MUSIC_CALM1,
};

/// Generates and manages procedural audio buffers.
class AudioRegistry {
public:
  AudioRegistry();

  /// Generate all built-in procedural sounds.
  void seedBuiltinSounds(int32_t sampleRate = 44100);

  /// Get a buffer by ID, or nullptr.
  [[nodiscard]] auto get(SoundId id) const -> const AudioBuffer*;

  /// Register a custom buffer.
  void register_(SoundId id, AudioBuffer buffer);

private:
  static auto createNoiseBuffer(int32_t sampleRate, float duration,
                                 float decayPower, float toneMix) -> AudioBuffer;
  static auto createToneBuffer(int32_t sampleRate, float duration,
                                float baseFreq, float wobble) -> AudioBuffer;

  std::unordered_map<int32_t, AudioBuffer> m_buffers;
  std::mt19937 m_rng{42};
};

/// Minimal audio engine that can play one-shot spatial sounds.
/// This is a stub that compiles — full miniaudio integration needs the header.
class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();

  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  /// Play a one-shot sound at a 3D position.
  void playOneShot(const AudioBuffer& buffer, float x, float y, float z,
                   float volume = 1.0f, float pitch = 1.0f);

  /// Set the listener position/orientation for spatial audio.
  void setListenerPosition(float x, float y, float z);
  void setListenerOrientation(float fx, float fy, float fz, float ux, float uy, float uz);

  /// Set master volume.
  void setMasterVolume(float vol);

  /// Per-frame update.
  void update(float dt);

  [[nodiscard]] auto isInitialized() const -> bool { return m_initialized; }

private:
  bool m_initialized = false;
  float m_masterVolume = 0.85f;

  struct ActiveSound {
    std::vector<float> samples;
    int32_t sampleRate;
    size_t position = 0;
    float volume;
    float pitch;
    float x, y, z;
  };
  std::vector<ActiveSound> m_activeSounds;
};

} // namespace voxel::audio
