#pragma once

#include <cstdint>
#include <vector>

namespace terrain {

/// Tag component store - no data, just presence/absence.
class TagStore {
public:
  explicit TagStore(int32_t capacity) : m_sparse(capacity, -1), m_dense(capacity, 0) {}

  [[nodiscard]] auto has(int32_t entityIndex) const -> bool {
    return m_sparse[entityIndex] != -1;
  }

  void add(int32_t entityIndex) {
    if (m_sparse[entityIndex] != -1) return;
    int32_t row = m_denseCount++;
    m_sparse[entityIndex] = row;
    m_dense[row] = entityIndex;
  }

  void remove(int32_t entityIndex) {
    int32_t row = m_sparse[entityIndex];
    if (row == -1) return;
    int32_t last = --m_denseCount;
    if (row != last) {
      int32_t movedEntity = m_dense[last];
      m_dense[row] = movedEntity;
      m_sparse[movedEntity] = row;
    }
    m_sparse[entityIndex] = -1;
  }

  [[nodiscard]] auto entityAtRow(int32_t row) const -> int32_t { return m_dense[row]; }
  [[nodiscard]] auto count() const -> int32_t { return m_denseCount; }

  template <typename F>
  void forEach(F&& callback) {
    for (int32_t row = 0; row < m_denseCount; ++row) {
      callback(row, m_dense[row]);
    }
  }

private:
  std::vector<int32_t> m_sparse;
  std::vector<int32_t> m_dense;
  int32_t m_denseCount = 0;
};

} // namespace terrain
