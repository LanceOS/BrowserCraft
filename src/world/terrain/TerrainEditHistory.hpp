#pragma once

#include "TerrainBrush.hpp"
#include <vector>
#include <mutex>
#include <cstdint>

namespace terrain {

/// Represents a single manual terrain edit.
struct TerrainEdit {
  TerrainBrush brush;
  uint64_t timestamp = 0;
};

/// A thread-safe history log of all manual terrain modifications.
class TerrainEditHistory {
public:
  TerrainEditHistory() = default;
  ~TerrainEditHistory() = default;

  TerrainEditHistory(const TerrainEditHistory&) = delete;
  TerrainEditHistory& operator=(const TerrainEditHistory&) = delete;

  /// Add a new edit to the history log.
  void addEdit(const TerrainBrush& brush, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edits.push_back({brush, timestamp});
  }

  /// Add an existing edit to the history log.
  void addEdit(const TerrainEdit& edit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edits.push_back(edit);
  }

  /// Get a copy of the entire edit history.
  [[nodiscard]] auto getEdits() const -> std::vector<TerrainEdit> {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_edits;
  }

  /// Clear all edits in the log.
  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edits.clear();
  }

private:
  std::vector<TerrainEdit> m_edits;
  mutable std::mutex m_mutex;
};

} // namespace terrain
