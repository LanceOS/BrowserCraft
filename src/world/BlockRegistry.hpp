#pragma once

#include "BlockDefinition.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <stdexcept>

namespace voxel {

/// Registry for all block definitions in the game.
class BlockRegistry {
public:
  explicit BlockRegistry(uint32_t capacity = 4096)
    : m_defs(capacity, nullptr) {}

  /// Register a block definition. Throws if ID is already taken.
  void register_(BlockDefinition def) {
    if (m_defs[def.id]) {
      throw std::runtime_error("Block id " + std::to_string(def.id) + " already registered");
    }
    auto* ptr = new BlockDefinition(std::move(def));
    m_defs[ptr->id] = ptr;
    m_byName[ptr->name] = ptr->id;
  }

  /// Get a block definition by ID. Throws if not found.
  [[nodiscard]] auto get(uint32_t id) const -> const BlockDefinition& {
    auto* def = m_defs[id];
    if (!def) throw std::runtime_error("Unknown block id " + std::to_string(id));
    return *def;
  }

  /// Try to get a block definition. Returns nullptr if not found.
  [[nodiscard]] auto tryGet(uint32_t id) const -> const BlockDefinition* {
    return m_defs[id];
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
    for (auto* def : m_defs) {
      if (def) callback(*def);
    }
  }

  [[nodiscard]] auto capacity() const -> uint32_t { return static_cast<uint32_t>(m_defs.size()); }

  ~BlockRegistry() {
    for (auto* def : m_defs) delete def;
  }

  // Non-copyable, movable
  BlockRegistry(const BlockRegistry&) = delete;
  BlockRegistry& operator=(const BlockRegistry&) = delete;
  BlockRegistry(BlockRegistry&&) = default;
  BlockRegistry& operator=(BlockRegistry&&) = default;

private:
  std::vector<BlockDefinition*> m_defs;
  std::unordered_map<std::string, uint32_t> m_byName;
};

} // namespace voxel
