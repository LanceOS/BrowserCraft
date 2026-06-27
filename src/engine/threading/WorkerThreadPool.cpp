#include "WorkerThreadPool.hpp"

namespace terrain {

WorkerThreadPool::WorkerThreadPool(int32_t numThreads) {
  for (int32_t i = 0; i < numThreads; ++i) {
    m_threads.emplace_back([this]() {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock lock(m_mutex);
          m_cv.wait(lock, [this]() { return m_stop.load() || !m_tasks.empty(); });
          if (m_stop.load() && m_tasks.empty()) return;
          task = std::move(m_tasks.front());
          m_tasks.pop();
          ++m_activeTasks;
        }
        task();
        {
          std::lock_guard lock(m_mutex);
          if (m_activeTasks > 0) {
            --m_activeTasks;
          }
          if (m_tasks.empty() && m_activeTasks == 0) {
            m_idleCv.notify_all();
          }
        }
      }
    });
  }
}

WorkerThreadPool::~WorkerThreadPool() {
  m_stop.store(true);
  m_cv.notify_all();
  for (auto& t : m_threads) {
    if (t.joinable()) t.join();
  }
}

void WorkerThreadPool::waitIdle() {
  std::unique_lock lock(m_mutex);
  m_idleCv.wait(lock, [this]() {
    return m_tasks.empty() && m_activeTasks == 0;
  });
}

} // namespace terrain
