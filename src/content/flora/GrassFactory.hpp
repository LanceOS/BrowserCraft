#pragma once

#include "FloraCategoryFactory.hpp"
#include <unordered_map>

namespace voxel::flora {

/// Definition for a single grass species (e.g., tall grass, fern).
struct GrassSpeciesDefinition {
  uint16_t speciesId = 0;
  std::string name;
  uint16_t blockId = 0;
  uint16_t textureLayer = 0;
  std::vector<SoilType> acceptableSoil;
  std::vector<std::string> biomeAffinity;  /// Biome names where this grass spawns.
  float spawnChance = 0.1f;  /// 0.0 - 1.0 probability per eligible surface block
  bool isTall = false;        /// Whether this is a 2-block-tall plant
  bool dropsSeeds = false;
  uint16_t seedItemId = 0;
};

/// Concrete factory for grass-type flora (tall grass, ferns, etc.).
/// Grass blocks are registered in blocks.json and rendered as foliage cubes
/// (or cross-quads once the mesher supports that render type).
class GrassFactory final : public FloraCategoryFactory {
public:
  explicit GrassFactory(std::vector<GrassSpeciesDefinition> grassList);

  void registerBlocks(BlockRegistry& registry) override;

  [[nodiscard]] FloraProperties getProperties(uint16_t speciesId) const override;

  [[nodiscard]] std::vector<FloraProperties> getAllProperties() const override;

  [[nodiscard]] std::string_view categoryName() const noexcept override { return "grass"; }

  /// Get the full species definition for a given species ID.
  [[nodiscard]] const GrassSpeciesDefinition* getSpecies(uint16_t speciesId) const;

private:
  std::vector<GrassSpeciesDefinition> m_grassList;
  std::unordered_map<uint16_t, size_t> m_speciesIndex;  /// speciesId → index in m_grassList
};

} // namespace voxel::flora
