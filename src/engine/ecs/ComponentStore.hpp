#pragma once

#include <vector>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <utility>

namespace voxel {

/// Generic component store with sparse/dense mapping for O(1) lookup.
/// T must be default-constructible and movable.
template <typename T>
class ComponentStore {
  static_assert(std::is_default_constructible_v<T>, "Component type must be default-constructible");

public:
  explicit ComponentStore(int32_t capacity)
    : m_sparse(capacity, -1), m_dense(capacity, 0), m_data(capacity) {}

  /// Get the row index for an entity, or -1 if it doesn't have this component.
  [[nodiscard]] auto rowFor(int32_t entityIndex) const -> int32_t {
    return m_sparse[entityIndex];
  }

  /// Check if an entity has this component.
  [[nodiscard]] auto has(int32_t entityIndex) const -> bool {
    return m_sparse[entityIndex] != -1;
  }

  /// Add a component for an entity. Returns the row index.
  /// If already added, returns the existing row.
  auto add(int32_t entityIndex) -> int32_t {
    int32_t existing = m_sparse[entityIndex];
    if (existing != -1) return existing;
    int32_t row = m_denseCount++;
    m_sparse[entityIndex] = row;
    m_dense[row] = entityIndex;
    m_data[row] = T{}; // default-initialize
    return row;
  }

  /// Add a component with a specific value. Returns the row index.
  auto add(int32_t entityIndex, T value) -> int32_t {
    int32_t row = add(entityIndex);
    m_data[row] = std::move(value);
    return row;
  }

  /// Remove a component from an entity.
  void remove(int32_t entityIndex) {
    int32_t row = m_sparse[entityIndex];
    if (row == -1) return;
    int32_t last = --m_denseCount;

    if (row != last) {
      // Move the last element into the removed slot
      m_data[row] = std::move(m_data[last]);
      int32_t movedEntity = m_dense[last];
      m_dense[row] = movedEntity;
      m_sparse[movedEntity] = row;
    }

    m_sparse[entityIndex] = -1;
  }

  /// Get a mutable reference to the component data for an entity.
  /// Asserts that the entity has this component.
  [[nodiscard]] auto get(int32_t entityIndex) -> T& {
    int32_t row = m_sparse[entityIndex];
    assert(row != -1 && "Entity does not have this component");
    return m_data[row];
  }

  /// Get a const reference to the component data for an entity.
  [[nodiscard]] auto get(int32_t entityIndex) const -> const T& {
    int32_t row = m_sparse[entityIndex];
    assert(row != -1 && "Entity does not have this component");
    return m_data[row];
  }

  /// Try to get component data. Returns nullptr if entity doesn't have it.
  [[nodiscard]] auto tryGet(int32_t entityIndex) -> T* {
    int32_t row = m_sparse[entityIndex];
    return row != -1 ? &m_data[row] : nullptr;
  }

  /// Get raw data by row index.
  [[nodiscard]] auto dataAtRow(int32_t row) -> T& { return m_data[row]; }
  [[nodiscard]] auto dataAtRow(int32_t row) const -> const T& { return m_data[row]; }

  /// Get the entity index at a given row.
  [[nodiscard]] auto entityAtRow(int32_t row) const -> int32_t { return m_dense[row]; }

  /// Number of entities with this component.
  [[nodiscard]] auto count() const -> int32_t { return m_denseCount; }

  /// Iterate over all rows. Callback receives (rowIndex, entityIndex, data&).
  template <typename F>
  void forEach(F&& callback) {
    for (int32_t row = 0; row < m_denseCount; ++row) {
      callback(row, m_dense[row], m_data[row]);
    }
  }

  template <typename F>
  void forEach(F&& callback) const {
    for (int32_t row = 0; row < m_denseCount; ++row) {
      callback(row, m_dense[row], m_data[row]);
    }
  }

private:
  std::vector<int32_t> m_sparse; // entityIndex → rowIndex, -1 = not present
  std::vector<int32_t> m_dense;  // rowIndex → entityIndex
  std::vector<T> m_data;         // rowIndex → component data
  int32_t m_denseCount = 0;
};

} // namespace voxel
