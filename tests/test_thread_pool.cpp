#include <catch2/catch_test_macros.hpp>
#include "engine/threading/WorkerThreadPool.hpp"
#include <atomic>

TEST_CASE("WorkerThreadPool executes jobs", "[threading]") {
  terrain::WorkerThreadPool pool(2);
  REQUIRE(pool.threadCount() == 2);

  std::atomic<int> counter{0};

  auto f1 = pool.submit([&]() { counter.fetch_add(1); });
  auto f2 = pool.submit([&]() { counter.fetch_add(1); });

  f1.wait();
  f2.wait();
  REQUIRE(counter.load() == 2);
}

TEST_CASE("WorkerThreadPool returns results", "[threading]") {
  terrain::WorkerThreadPool pool(2);

  auto f = pool.submit([]() -> int { return 42; });
  REQUIRE(f.get() == 42);
}

TEST_CASE("WorkerThreadPool handles many jobs", "[threading]") {
  terrain::WorkerThreadPool pool(4);

  std::atomic<int> counter{0};
  constexpr int N = 100;

  std::vector<std::future<void>> futures;
  for (int i = 0; i < N; ++i) {
    futures.push_back(pool.submit([&]() { counter.fetch_add(1); }));
  }

  for (auto& f : futures) f.wait();
  REQUIRE(counter.load() == N);
}
