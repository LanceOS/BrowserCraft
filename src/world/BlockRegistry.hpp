#pragma once

#include "BlockDefinition.hpp"
#include <optional>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

// @deprecated Legacy voxel-world code retained during the render-only migration to triangle meshes.
namespace voxel {

/// Registry for all block definitions in the game.
class BlockRegistry {
public:
  explicit BlockRegistry(uint32_t capacity = 4096)
    : m_defs(capacity) {}

  /// Register a block definition. Throws if ID is already taken.
  void register_(BlockDefinition def) {
    if (def.id >= m_defs.size()) {
      throw std::runtime_error("Block id " + std::to_string(def.id) + " is out of range");
    }
    if (m_defs[def.id]) {
      throw std::runtime_error("Block id " + std::to_string(def.id) + " already registered");
    }
    auto& stored = m_defs[def.id].emplace(std::move(def));
    m_byName[stored.name] = stored.id;
  }

  /// Get a block definition by ID. Throws if not found.
  [[nodiscard]] auto get(uint32_t id) const -> const BlockDefinition& {
    auto* def = tryGet(id);
    if (!def) throw std::runtime_error("Unknown block id " + std::to_string(id));
    return *def;
  }

  /// Try to get a block definition. Returns nullptr if not found.
  [[nodiscard]] auto tryGet(uint32_t id) const -> const BlockDefinition* {
    if (id >= m_defs.size() || !m_defs[id]) return nullptr;
    return &*m_defs[id];
  }

  /// Look up a block ID by name. Returns nullopt if not found.
  [[nodiscard]] auto idByName(const std::string& name) const -> std::optional<uint32_t> {
    auto it = m_byName.find(name);
    if (it != m_byName.end()) return it->second;
    return std::nullopt;
  }

  /// Iterate over all registered blocks.
  template <typename F>
  void forEach(F&& callback) const {
    for (const auto& def : m_defs) {
      if (def) callback(*def);
    }
  }

  [[nodiscard]] auto capacity() const -> uint32_t { return static_cast<uint32_t>(m_defs.size()); }

  // Non-copyable, movable
  BlockRegistry(const BlockRegistry&) = delete;
  BlockRegistry& operator=(const BlockRegistry&) = delete;
  BlockRegistry(BlockRegistry&&) = default;
  BlockRegistry& operator=(BlockRegistry&&) = default;

private:
  std::vector<std::optional<BlockDefinition>> m_defs;
  std::unordered_map<std::string, uint32_t> m_byName;
};

} // namespace voxel
