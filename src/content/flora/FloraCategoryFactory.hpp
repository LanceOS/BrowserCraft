#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "FloraTypes.hpp"
#include "world/BlockRegistry.hpp"

namespace voxel::flora {

/// Abstract factory for a class of flora.
/// Each concrete implementation (e.g., GrassFactory, FlowerFactory, CropFactory)
/// registers its blocks and exposes its property table for use by worldgen and
/// gameplay systems.
class FloraCategoryFactory {
public:
  virtual ~FloraCategoryFactory() = default;

  /// Register all blocks in this category with the BlockRegistry.
  /// Called once at startup during flora initialization.
  virtual void registerBlocks(BlockRegistry& registry) = 0;

  /// Return the FloraProperties for a given species ID within this category.
  [[nodiscard]] virtual FloraProperties getProperties(uint16_t speciesId) const = 0;

  /// Return all species definitions in this category.
  [[nodiscard]] virtual std::vector<FloraProperties> getAllProperties() const = 0;

  /// The category name (e.g., "trees", "flowers", "grass").
  [[nodiscard]] virtual std::string_view categoryName() const noexcept = 0;
};

} // namespace voxel::flora
