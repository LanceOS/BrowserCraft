#pragma once

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

/// Interface for game systems.
template <typename TState>
struct System {
  virtual ~System() = default;

  /// Human-readable name for debugging.
  [[nodiscard]] virtual auto name() const -> const std::string& = 0;

  /// Execution stage determines ordering.
  [[nodiscard]] virtual auto stage() const -> SystemStage = 0;

  /// Called each tick.
  virtual void update(TState& state, float dt) = 0;
};

/// Manages an ordered collection of systems, executed by stage each tick.
template <typename TState>
class SystemManager {
public:
  /// Add a system. Systems are sorted by stage after insertion.
  void add(std::unique_ptr<System<TState>> system) {
    m_systems.push_back(std::move(system));
    sort();
  }

  /// Run all systems in stage order.
  void update(TState& state, float dt) {
    for (auto& sys : m_systems) {
      sys->update(state, dt);
    }
  }

  [[nodiscard]] auto count() const -> size_t { return m_systems.size(); }

private:
  void sort() {
    std::sort(m_systems.begin(), m_systems.end(),
      [](const auto& a, const auto& b) {
        return stageOrder(a->stage()) < stageOrder(b->stage());
      });
  }

  std::vector<std::unique_ptr<System<TState>>> m_systems;
};

} // namespace voxel
