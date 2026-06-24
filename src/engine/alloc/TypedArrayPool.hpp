#pragma once

#include <vector>
#include <memory>
#include <functional>

namespace voxel {

/// Simple object pool that reuses previously released objects.
template <typename T>
class TypedArrayPool {
public:
  /// @param factory A function that creates a new T when the pool is empty.
  explicit TypedArrayPool(std::function<std::unique_ptr<T>()> factory)
    : m_factory(std::move(factory)) {}

  /// Acquire an object from the pool, or create a new one.
  auto acquire() -> std::unique_ptr<T> {
    if (m_pool.empty()) {
      return m_factory();
    }
    auto obj = std::move(m_pool.back());
    m_pool.pop_back();
    return obj;
  }

  /// Return an object to the pool for reuse.
  void release(std::unique_ptr<T> value) {
    m_pool.push_back(std::move(value));
  }

  [[nodiscard]] auto available() const -> size_t { return m_pool.size(); }

private:
  std::vector<std::unique_ptr<T>> m_pool;
  std::function<std::unique_ptr<T>()> m_factory;
};

} // namespace voxel
