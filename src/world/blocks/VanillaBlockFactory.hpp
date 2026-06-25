#pragma once

#include "BlockFactory.hpp"

namespace voxel {

/// Concrete factory that populates the BlockRegistry with vanilla blocks
/// sourced from assets/blocks.json via the AssetManager.
///
/// Material flags (liquid, foliage, light emission) and game properties
/// (hardness, blast resistance) are read directly from the JSON definition.
class VanillaBlockFactory final : public BlockFactory {
public:
  /// Load all block definitions from assets/blocks.json and register them.
  void registerAll(BlockRegistry& registry) override;
};

} // namespace voxel
