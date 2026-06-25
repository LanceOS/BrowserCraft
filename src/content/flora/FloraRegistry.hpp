#pragma once

#include "FloraCategoryFactory.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace voxel::flora {

/// Aggregates all flora category factories and provides unified lookup
/// for worldgen and gameplay systems.
class FloraRegistry {
public:
  /// Register a category factory. Takes ownership.
  void registerCategory(std::unique_ptr<FloraCategoryFactory> factory);

  /// Get a category factory by name. Returns nullptr if not found.
  [[nodiscard]] FloraCategoryFactory* getCategory(std::string_view name) const;

  /// Get flora properties for a given block ID. Returns nullptr if not flora.
  [[nodiscard]] const FloraProperties* getProperties(uint16_t blockId) const;

  /// Returns true if the given block ID is registered flora.
  [[nodiscard]] bool isFlora(uint16_t blockId) const;

  /// Register all blocks from all registered categories with the BlockRegistry.
  void registerAllBlocks(BlockRegistry& registry);

  /// Get all registered category factories.
  [[nodiscard]] const std::vector<std::unique_ptr<FloraCategoryFactory>>& getAllCategories() const {
    return m_categories;
  }

private:
  std::vector<std::unique_ptr<FloraCategoryFactory>> m_categories;
  std::unordered_map<std::string, FloraCategoryFactory*> m_byName;
  std::unordered_map<uint16_t, FloraProperties> m_byBlockId;
};

} // namespace voxel::flora
