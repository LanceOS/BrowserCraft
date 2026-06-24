#include <catch2/catch_test_macros.hpp>
#include "engine/alloc/LockFreeRingBuffer.hpp"

TEST_CASE("WorkerCompletionQueue push and poll", "[alloc]") {
  voxel::WorkerCompletionQueue q(4);

  REQUIRE_FALSE(q.poll().has_value());

  REQUIRE(q.push(42));
  REQUIRE(q.push(99));

  REQUIRE(q.poll() == 42);
  REQUIRE(q.poll() == 99);
  REQUIRE_FALSE(q.poll().has_value());
}

TEST_CASE("WorkerCompletionQueue full rejects", "[alloc]") {
  voxel::WorkerCompletionQueue q(2);

  REQUIRE(q.push(1));
  REQUIRE(q.push(2));
  REQUIRE_FALSE(q.push(3)); // full

  REQUIRE(q.poll() == 1);
  REQUIRE(q.push(4)); // now has room
  REQUIRE(q.poll() == 2);
  REQUIRE(q.poll() == 4);
}

TEST_CASE("WorkerCompletionQueue wrap-around", "[alloc]") {
  voxel::WorkerCompletionQueue q(4);

  for (int i = 0; i < 100; ++i) {
    REQUIRE(q.push(i));
    REQUIRE(q.poll() == i);
    REQUIRE_FALSE(q.poll().has_value());
  }
}
