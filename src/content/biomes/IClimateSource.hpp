#pragma once

#include "BiomeData.hpp"

namespace voxel::biome {

/// Interface for climate noise sampling.
/// Implementations provide temperature and humidity at any world coordinate.
/// This decouples climate noise generation from the rest of the biome system,
/// allowing different noise backends (Simplex, flat preset, debug patterns)
/// to be swapped without modifying WorldGenPipeline or BiomeClassifier.
class IClimateSource {
public:
  virtual ~IClimateSource() = default;

  /// Sample temperature and humidity at the given world coordinate.
  /// Both values are in [0, 1].
  [[nodiscard]] virtual ClimateSample sampleClimate(float worldX, float worldZ) const = 0;
};

} // namespace voxel::biome
