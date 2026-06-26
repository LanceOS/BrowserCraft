#pragma once

#include "engine/audio/AudioEngine.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockIds.hpp"
#include <mutex>
#include <random>

namespace voxel {

/// Plays block interaction sounds (break, place, step).
class BlockInteractionAudio {
public:
  BlockInteractionAudio(audio::AudioEngine& engine,
                        audio::AudioRegistry& registry,
                        BlockRegistry& blocks)
    : m_engine(engine), m_registry(registry), m_blocks(blocks) {}

  /// Play the break sound for a block at world position.
  void onBlockBroken(float x, float y, float z, uint32_t blockId) {
    if (blockId == 0) return;
    auto* def = m_blocks.tryGet(blockId);
    if (!def) return;

    auto soundId = audio::SoundId::STONE_BREAK;
    if (blockId == BlockId::GRASS || blockId == BlockId::DIRT || def->material.foliage) {
      soundId = audio::SoundId::GRASS_BREAK;
    }

    auto* buffer = m_registry.get(soundId);
    if (!buffer) return;
    m_engine.playOneShot(*buffer, x + 0.5f, y + 0.5f, z + 0.5f, 1.0f, randomPitch(0.85f, 1.15f));
  }

  /// Play a step sound.
  void onStep(float x, float y, float z, uint32_t blockId) {
    if (blockId == 0) return;
    auto* def = m_blocks.tryGet(blockId);
    if (!def) return;

    auto soundId = audio::SoundId::STONE_STEP;
    if (blockId == BlockId::GRASS || blockId == BlockId::DIRT || def->material.foliage) {
      soundId = audio::SoundId::GRASS_STEP;
    }

    auto* buffer = m_registry.get(soundId);
    if (!buffer) return;
    m_engine.playOneShot(*buffer, x, y, z, 0.4f, randomPitch(0.9f, 1.1f));
  }

private:
  auto randomPitch(float minPitch, float maxPitch) -> float {
    std::lock_guard<std::mutex> lock(m_rngMutex);
    std::uniform_real_distribution<float> dist(minPitch, maxPitch);
    return dist(m_rng);
  }

  audio::AudioEngine& m_engine;
  audio::AudioRegistry& m_registry;
  BlockRegistry& m_blocks;
  std::mt19937 m_rng{std::random_device{}()};
  std::mutex m_rngMutex;
};

} // namespace voxel
