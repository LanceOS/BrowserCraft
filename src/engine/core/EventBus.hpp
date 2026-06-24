#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <any>

namespace voxel {

/// Type-erased event bus for game events.
class EventBus {
public:
  /// Subscribe to an event type. Returns an unsubscribe token.
  template <typename T>
  auto on(std::function<void(const T&)> listener) -> std::function<void()> {
    auto& bucket = m_listeners[std::type_index(typeid(T))];
    auto* wrapper = new TypedListener<T>{std::move(listener)};
    bucket.push_back(wrapper);
    return [&bucket, wrapper]() {
      auto it = std::find(bucket.begin(), bucket.end(), wrapper);
      if (it != bucket.end()) {
        delete static_cast<TypedListener<T>*>(*it);
        bucket.erase(it);
      }
    };
  }

  /// Emit an event to all listeners.
  template <typename T>
  void emit(const T& payload) {
    auto it = m_listeners.find(std::type_index(typeid(T)));
    if (it == m_listeners.end()) return;
    for (auto* base : it->second) {
      base->invoke(&payload);
    }
  }

  ~EventBus() {
    for (auto& [type, bucket] : m_listeners) {
      for (auto* base : bucket) delete base;
    }
  }

  EventBus() = default;
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

private:
  struct ListenerBase {
    virtual ~ListenerBase() = default;
    virtual void invoke(const void* payload) = 0;
  };

  template <typename T>
  struct TypedListener : ListenerBase {
    std::function<void(const T&)> fn;
    explicit TypedListener(std::function<void(const T&)> f) : fn(std::move(f)) {}
    void invoke(const void* payload) override { fn(*static_cast<const T*>(payload)); }
  };

  std::unordered_map<std::type_index, std::vector<ListenerBase*>> m_listeners;
};

} // namespace voxel
