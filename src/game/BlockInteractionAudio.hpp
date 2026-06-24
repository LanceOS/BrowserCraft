#pragma once

#include "engine/audio/AudioEngine.hpp"
#include "world/BlockRegistry.hpp"

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
    if (blockId == 2 || blockId == 3 || def->material.foliage) {
      soundId = audio::SoundId::GRASS_BREAK;
    } else if (blockId == 5 || blockId == 17 || blockId == 54 || blockId == 58) {
      soundId = audio::SoundId::WOOD_BREAK;
    }

    auto* buffer = m_registry.get(soundId);
    if (!buffer) return;
    m_engine.playOneShot(*buffer, x + 0.5f, y + 0.5f, z + 0.5f, 1.0f, 0.85f + 0.3f * (rand() / float(RAND_MAX)));
  }

  /// Play a step sound.
  void onStep(float x, float y, float z, uint32_t blockId) {
    if (blockId == 0) return;
    auto* def = m_blocks.tryGet(blockId);
    if (!def) return;

    auto soundId = audio::SoundId::STONE_STEP;
    if (blockId == 2 || blockId == 3 || def->material.foliage) {
      soundId = audio::SoundId::GRASS_STEP;
    }

    auto* buffer = m_registry.get(soundId);
    if (!buffer) return;
    m_engine.playOneShot(*buffer, x, y, z, 0.4f, 0.9f + 0.2f * (rand() / float(RAND_MAX)));
  }

private:
  audio::AudioEngine& m_engine;
  audio::AudioRegistry& m_registry;
  BlockRegistry& m_blocks;
};

} // namespace voxel
