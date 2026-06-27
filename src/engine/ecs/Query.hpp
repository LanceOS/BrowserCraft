#pragma once

#include "ComponentStore.hpp"
#include "TagStore.hpp"
#include <tuple>
#include <type_traits>
#include <vector>

namespace terrain {

/// Find entities that have ALL specified component types.
/// Returns a vector of matching entity indices.
template <typename... Stores>
auto queryEntities(Stores&... stores) -> std::vector<int32_t> {
  static_assert(sizeof...(Stores) > 0, "queryEntities requires at least one store");
  std::vector<int32_t> result;

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

} // namespace terrain
