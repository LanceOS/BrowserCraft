#pragma once

#include "engine/core/TickContext.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

namespace voxel {

enum class SystemStage {
  PrePhysics,
  Physics,
  PostPhysics,
  Render,
};

/// Order for sorting by stage.
inline auto stageOrder(SystemStage stage) -> int {
  switch (stage) {
    case SystemStage::PrePhysics: return 0;
    case SystemStage::Physics:    return 1;
    case SystemStage::PostPhysics: return 2;
    case SystemStage::Render:     return 3;
  }
  return 0;
}

/// Interface for game systems. Receives a TickContext instead of a Game
/// reference, making dependencies explicit and enabling unit testing.
struct System {
  virtual ~System() = default;

  /// Human-readable name for debugging.
  [[nodiscard]] virtual auto name() const -> const std::string& = 0;

  /// Execution stage determines ordering.
  [[nodiscard]] virtual auto stage() const -> SystemStage = 0;

  /// Called each tick with the per-frame context.
  virtual void update(TickContext& ctx) = 0;
};

/// Manages an ordered collection of systems, executed by stage each tick.
class SystemManager {
public:
  /// Add a system. Systems are sorted by stage after insertion.
  void add(std::unique_ptr<System> system) {
    m_systems.push_back(std::move(system));
    sort();
  }

  /// Run all systems in stage order.
  void update(TickContext& ctx) {
    for (auto& sys : m_systems) {
      sys->update(ctx);
    }
  }

  [[nodiscard]] auto count() const -> size_t { return m_systems.size(); }

  /// Remove all systems.
  void clear() { m_systems.clear(); }

private:
  void sort() {
    std::sort(m_systems.begin(), m_systems.end(),
      [](const auto& a, const auto& b) {
        return stageOrder(a->stage()) < stageOrder(b->stage());
      });
  }

  std::vector<std::unique_ptr<System>> m_systems;
};

} // namespace voxel
