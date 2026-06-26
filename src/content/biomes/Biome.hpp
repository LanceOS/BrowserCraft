#pragma once

#include "BiomeData.hpp"

namespace terrain::biome {

// ---------------------------------------------------------------------------
// Abstract biome interface
// ---------------------------------------------------------------------------

/// Polymorphic base for all biomes. Each concrete biome is a stateless
/// singleton that defines its terrain shaping parameters, and climate traits.
class Biome {
public:
  virtual ~Biome() = default;

  /// Unique identifier for this biome (BiomeId enum).
  [[nodiscard]] virtual BiomeId id() const noexcept = 0;

  [[nodiscard]] virtual int32_t surfaceDepth() const noexcept = 0;

  /// Height bias for terrain generation. Positive = higher terrain.
  [[nodiscard]] virtual float heightBias() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// Concrete biome singletons
// ---------------------------------------------------------------------------

class PlainsBiome final : public Biome {
public:
  static const PlainsBiome& instance() { static PlainsBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Plains; }
  int32_t surfaceDepth() const noexcept override { return 4; }
  float heightBias() const noexcept override { return kPlainsHeightBias; }
private:
  PlainsBiome() = default;
};

class DesertBiome final : public Biome {
public:
  static const DesertBiome& instance() { static DesertBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Desert; }
  int32_t surfaceDepth() const noexcept override { return 3; }
  float heightBias() const noexcept override { return kDesertHeightBias; }
private:
  DesertBiome() = default;
};

class ForestBiome final : public Biome {
public:
  static const ForestBiome& instance() { static ForestBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Forest; }
  int32_t surfaceDepth() const noexcept override { return 5; }
  float heightBias() const noexcept override { return kForestHeightBias; }
private:
  ForestBiome() = default;
};

class MountainsBiome final : public Biome {
public:
  static const MountainsBiome& instance() { static MountainsBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Mountains; }
  int32_t surfaceDepth() const noexcept override { return 3; }
  float heightBias() const noexcept override { return kMountainsHeightBias; }
private:
  MountainsBiome() = default;
};

class SwampBiome final : public Biome {
public:
  static const SwampBiome& instance() { static SwampBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Swamp; }
  int32_t surfaceDepth() const noexcept override { return 5; }
  float heightBias() const noexcept override { return kSwampHeightBias; }
private:
  SwampBiome() = default;
};

class OceanBiome final : public Biome {
public:
  static const OceanBiome& instance() { static OceanBiome s; return s; }
  BiomeId id() const noexcept override { return BiomeId::Ocean; }
  int32_t surfaceDepth() const noexcept override { return 3; }
  float heightBias() const noexcept override { return kOceanHeightBias; }
private:
  OceanBiome() = default;
};

} // namespace terrain::biome
