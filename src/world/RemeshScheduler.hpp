#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace voxel {

/// Collects boundary edits during a frame and flushes each unique chunk once.
class RemeshScheduler {
public:
  void noteBoundaryEdit(int32_t chunkX, int32_t chunkZ) {
    m_boundaryEdits.emplace_back(chunkX, chunkZ);
  }

  template <typename F>
  void flush(F&& onChunk) {
    if (m_boundaryEdits.empty()) return;

    std::sort(m_boundaryEdits.begin(), m_boundaryEdits.end());
    auto last = std::unique(m_boundaryEdits.begin(), m_boundaryEdits.end());
    for (auto it = m_boundaryEdits.begin(); it != last; ++it) {
      onChunk(it->first, it->second);
    }
    m_boundaryEdits.clear();
  }

  void clear() { m_boundaryEdits.clear(); }

  [[nodiscard]] auto empty() const -> bool { return m_boundaryEdits.empty(); }

private:
  std::vector<std::pair<int32_t, int32_t>> m_boundaryEdits;
};

} // namespace voxel
