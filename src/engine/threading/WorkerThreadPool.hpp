#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>

namespace voxel {

/// Simple thread pool for running world-gen and mesh jobs in parallel.
class WorkerThreadPool {
public:
  explicit WorkerThreadPool(int32_t numThreads);
  ~WorkerThreadPool();

  WorkerThreadPool(const WorkerThreadPool&) = delete;
  WorkerThreadPool& operator=(const WorkerThreadPool&) = delete;

  /// Submit a job. Returns a future for the result.
  template <typename F>
  auto submit(F&& job) -> std::future<std::invoke_result_t<F>> {
    using Result = std::invoke_result_t<F>;
    auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(job));
    auto future = task->get_future();

    {
      std::lock_guard lock(m_mutex);
      m_tasks.emplace([task]() { (*task)(); });
    }
    m_cv.notify_one();
    return future;
  }

  /// Submit a void-returning job without creating a packaged_task (avoids heap allocation).
  void submitAndForget(std::function<void()> job) {
    {
      std::lock_guard lock(m_mutex);
      m_tasks.emplace(std::move(job));
    }
    m_cv.notify_one();
  }

  /// Number of worker threads.
  [[nodiscard]] auto threadCount() const -> int32_t { return static_cast<int32_t>(m_threads.size()); }

  /// Check if any threads are busy (heuristic: queue not empty).
  [[nodiscard]] auto hasPending() const -> bool {
    std::lock_guard lock(m_mutex);
    return !m_tasks.empty();
  }

private:
  std::vector<std::thread> m_threads;
  std::queue<std::function<void()>> m_tasks;
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::atomic<bool> m_stop{false};
};

} // namespace voxel
