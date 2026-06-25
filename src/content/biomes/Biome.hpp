#pragma once

#include "BiomeData.hpp"
#include "world/BlockIds.hpp"

namespace voxel::biome {

// ---------------------------------------------------------------------------
// Abstract biome interface
// ---------------------------------------------------------------------------

/// Polymorphic base for all biomes. Each concrete biome is a stateless
/// singleton that defines its surface blocks, terrain shaping parameters,
/// and — in future phases — vegetation, mob spawning, weather, and events.
///
/// New biomes are added by creating a class that inherits from Biome and
/// registering it in BiomeFactory.  No existing code needs modification
/// (Open/Closed principle).
class Biome {
public:
  virtual ~Biome() = default;

  /// Unique identifier for this biome (BiomeId enum).
  [[nodiscard]] virtual BiomeId id() const noexcept = 0;

  /// Surface blocks — currently constant per biome, but can become
  /// contextual (e.g. seasonal, elevation-based) in the future.
  [[nodiscard]] virtual uint8_t topBlock() const noexcept = 0;
  [[nodiscard]] virtual uint8_t fillerBlock() const noexcept = 0;
  [[nodiscard]] virtual int32_t surfaceDepth() const noexcept = 0;

  /// Height bias for terrain generation.  Positive = higher terrain.
  [[nodiscard]] virtual float heightBias() const noexcept = 0;

  // ---- Future extension points ----
  // virtual float terrainNoiseScale() const;
  // virtual float terrainNoiseAmplitude() const;
  // virtual void decorate(ChunkSlot&, uint32_t seed) const;
  // virtual MobTable mobs() const;
  // virtual WeatherParams weather() const;
};

// ---------------------------------------------------------------------------
// Concrete biome singletons
// ---------------------------------------------------------------------------

class PlainsBiome final : public Biome {
public:
  static const PlainsBiome& instance() { static PlainsBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Plains; }
  uint8_t topBlock() const noexcept override { return BlockId::GRASS; }
  uint8_t fillerBlock() const noexcept override { return BlockId::DIRT; }
  int32_t surfaceDepth() const noexcept override { return 4; }
  float heightBias() const noexcept override { return kPlainsHeightBias; }
private:
  PlainsBiome() = default;
};

class DesertBiome final : public Biome {
public:
  static const DesertBiome& instance() { static DesertBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Desert; }
  uint8_t topBlock() const noexcept override { return BlockId::SAND; }
  uint8_t fillerBlock() const noexcept override { return BlockId::SAND; }
  int32_t surfaceDepth() const noexcept override { return 3; }
  float heightBias() const noexcept override { return kDesertHeightBias; }
private:
  DesertBiome() = default;
};

class ForestBiome final : public Biome {
public:
  static const ForestBiome& instance() { static ForestBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Forest; }
  uint8_t topBlock() const noexcept override { return BlockId::GRASS; }
  uint8_t fillerBlock() const noexcept override { return BlockId::DIRT; }
  int32_t surfaceDepth() const noexcept override { return 5; }
  float heightBias() const noexcept override { return kForestHeightBias; }
private:
  ForestBiome() = default;
};

class MountainsBiome final : public Biome {
public:
  static const MountainsBiome& instance() { static MountainsBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Mountains; }
  uint8_t topBlock() const noexcept override { return BlockId::GRASS; }
  uint8_t fillerBlock() const noexcept override { return BlockId::STONE; }
  int32_t surfaceDepth() const noexcept override { return 3; }
  float heightBias() const noexcept override { return kMountainsHeightBias; }
private:
  MountainsBiome() = default;
};

class SwampBiome final : public Biome {
public:
  static const SwampBiome& instance() { static SwampBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Swamp; }
  uint8_t topBlock() const noexcept override { return BlockId::STONE; }
  uint8_t fillerBlock() const noexcept override { return BlockId::STONE; }
  int32_t surfaceDepth() const noexcept override { return 5; }
  float heightBias() const noexcept override { return kSwampHeightBias; }
private:
  SwampBiome() = default;
};

class OceanBiome final : public Biome {
public:
  static const OceanBiome& instance() { static OceanBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Ocean; }
  uint8_t topBlock() const noexcept override { return BlockId::SAND; }
  uint8_t fillerBlock() const noexcept override { return BlockId::SAND; }
  int32_t surfaceDepth() const noexcept override { return 3; }
  float heightBias() const noexcept override { return kOceanHeightBias; }
private:
  OceanBiome() = default;
};

} // namespace voxel::biome
