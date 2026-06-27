#pragma once

#include <cstdint>
#include <array>

namespace terrain::biome {

/// A single temperature+humidity sample at a world position.
/// Bundling both values avoids redundant noise evaluations when
/// multiple consumers (sampleBiome, blendedHeightBias, mountainWeight)
/// need the same climate data for the same coordinate.
struct ClimateSample {
  float temperature;
  float humidity;
};

/// Per-biome weights for a given climate sample.
/// All weights are in [0, 1] and sum to 1 after normalisation.
/// Exposed so other systems (flora, mob spawning) can determine which
/// biomes are present at a location without duplicating blending logic.
struct BiomeWeights {
  float plains    = 0.0f;
  float desert    = 0.0f;
  float forest    = 0.0f;
  float mountains = 0.0f;
  float swamp     = 0.0f;
  float ocean     = 0.0f;
};

/// Numeric biome ID — replaces string-based biome name comparisons.
/// Enables O(1) lookups and fully constexpr surface rule definitions.
enum class BiomeId : uint8_t {
  Plains = 0,
  Desert,
  Forest,
  Mountains,
  Swamp,
  Ocean,
};

// ---------------------------------------------------------------------------
// Named threshold constants — single source of truth.
// Used by BiomeClassifier and BiomeFactory wrappers.
// ---------------------------------------------------------------------------

inline constexpr float kClimateScale           = 0.008f;

inline constexpr float kMountainTempThreshold  = 0.28f;
inline constexpr float kMountainTransWidth     = 0.08f;

inline constexpr float kDesertTempThreshold    = 0.65f;
inline constexpr float kDesertHumidThreshold   = 0.35f;
inline constexpr float kDesertTransWidth       = 0.08f;

inline constexpr float kSwampHumidThreshold    = 0.72f;
inline constexpr float kSwampHumidTransWidth   = 0.10f;
inline constexpr float kSwampTempThreshold     = 0.35f;
inline constexpr float kSwampTempTransWidth    = 0.08f;

inline constexpr float kForestHumidThreshold   = 0.55f;
inline constexpr float kForestTransWidth       = 0.08f;

inline constexpr float kOceanTempThreshold     = 0.42f;
inline constexpr float kOceanHumidThreshold    = 0.40f;
inline constexpr float kOceanTransWidth        = 0.08f;

inline constexpr float kPlainsHeightBias       = 0.0f;
inline constexpr float kDesertHeightBias       = -3.0f;
inline constexpr float kForestHeightBias       = 3.0f;
inline constexpr float kMountainsHeightBias    = 22.0f;
inline constexpr float kSwampHeightBias        = -3.0f;
inline constexpr float kOceanHeightBias        = -14.0f;

/// Hermite smoothstep: maps [threshold - width/2, threshold + width/2]
/// to [0, 1] with smooth ease-in-out. Values outside the transition
/// range clamp to 0 or 1 at compile time.
inline constexpr float smoothEdge(float value, float threshold, float width) {
  float edge = (value - threshold + width * 0.5f) / width;
  if (edge < 0.0f) edge = 0.0f;
  if (edge > 1.0f) edge = 1.0f;
  return edge * edge * (3.0f - 2.0f * edge);
}

/// Enum-to-enum array for O(1) BiomeId → Biome lookup via BiomeFactory::forId().
/// Contents must match BiomeId enum order — enforced by test.
inline constexpr std::array ALL_BIOME_IDS = {
  BiomeId::Plains,
  BiomeId::Desert,
  BiomeId::Forest,
  BiomeId::Mountains,
  BiomeId::Swamp,
  BiomeId::Ocean,
};

} // namespace terrain::biome
