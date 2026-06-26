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
        }
        task();
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

} // namespace terrain
