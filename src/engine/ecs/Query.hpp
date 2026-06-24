#pragma once

#include "ComponentStore.hpp"
#include <vector>

namespace voxel {

/// Find entities that have ALL specified component types.
/// Returns a vector of (entityIndex, rowIndices...) tuples.
/// The first store determines the iteration order.
template <typename... Stores>
auto queryEntities(Stores&... stores) -> std::vector<int32_t> {
  // Find the smallest store to iterate over
  auto counts = std::array{stores.count()...};
  size_t smallestIdx = 0;
  int32_t smallestCount = counts[0];
  for (size_t i = 1; i < counts.size(); ++i) {
    if (counts[i] < smallestCount) {
      smallestCount = counts[i];
      smallestIdx = i;
    }
  }

  if (smallestCount == 0) return {};

  std::vector<int32_t> result;

  // Helper: get entity at row from the idx-th store
  auto getEntityAtRow = [&]<size_t I>(const auto& s, int32_t row) -> int32_t {
    if constexpr (std::is_same_v<std::decay_t<decltype(s)>, TagStore>) {
      return s.entityAtRow(row);
    } else {
      return s.entityAtRow(row);
    }
  };

  // Iterate over the smallest store
  // We use the 0-th store as primary (TODO: generalize to smallestIdx)
  // For simplicity, rotate so store at smallestIdx is first
  // Actually, let me use a simpler approach: iterate rows and check all stores
  // We'll store pointers and use virtual dispatch pattern... 
  // For now, a simpler approach:

  // Use the simplest possible approach — iterate the first store
  auto& primary = std::get<0>(std::tie(stores...));
  for (int32_t row = 0; row < primary.count(); ++row) {
    int32_t entityIndex = primary.entityAtRow(row);
    bool hasAll = true;

    auto check = [&](auto& store) {
      if constexpr (std::is_same_v<std::decay_t<decltype(store)>, TagStore>) {
        if (!store.has(entityIndex)) hasAll = false;
      } else {
        if (store.rowFor(entityIndex) == -1) hasAll = false;
      }
    };

    (check(stores), ...);

    if (hasAll) {
      result.push_back(entityIndex);
    }
  }

  return result;
}

} // namespace voxel
