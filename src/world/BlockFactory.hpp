#pragma once

#include "world/BlockRegistry.hpp"

namespace voxel {

/// Abstract factory contract: subclasses decide HOW block definitions are
/// sourced (hard-coded vanilla, JSON-mod-loaded, network-synced).
///
/// The Registry is the singleton lookup; the Factory is the producer that
/// populates it at startup.
class BlockFactory {
public:
  virtual ~BlockFactory() = default;

  /// Register all blocks into the given registry.
  /// Called once at startup before any world is loaded.
  virtual void registerAll(BlockRegistry& registry) = 0;
};

} // namespace voxel
