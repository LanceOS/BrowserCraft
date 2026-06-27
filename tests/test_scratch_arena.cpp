#include <catch2/catch_test_macros.hpp>
#include "engine/alloc/ScratchArena.hpp"

TEST_CASE("ScratchArena basic allocation", "[alloc]") {
  terrain::ScratchArena arena(1024);

  auto* floats = arena.alloc<float>(100);
  REQUIRE(floats != nullptr);
  // All values should be zero-initialized
  for (int i = 0; i < 100; ++i) {
    REQUIRE(floats[i] == 0.0f);
  }

  // Write and verify
  floats[0] = 3.14f;
  REQUIRE(floats[0] == 3.14f);
}

TEST_CASE("ScratchArena alignment", "[alloc]") {
  terrain::ScratchArena arena(1024);

  // Allocate 1 byte type, then a 4-byte type — should be aligned
  auto* bytes = arena.alloc<uint8_t>(1);
  auto addr1 = reinterpret_cast<uintptr_t>(bytes);
  auto* ints = arena.alloc<int32_t>(1);
  auto addr2 = reinterpret_cast<uintptr_t>(ints);

  // Second allocation should be 4-byte aligned
  REQUIRE(addr2 % 4 == 0);
}

TEST_CASE("ScratchArena reset", "[alloc]") {
  terrain::ScratchArena arena(1024);

  auto* a = arena.alloc<float>(50);
  float* ptrA = a;
  arena.reset();
  auto* b = arena.alloc<float>(50);

  // After reset, should reuse the same memory
  REQUIRE(ptrA == b);
}

TEST_CASE("ScratchArena overflow throws", "[alloc]") {
  terrain::ScratchArena arena(64);

  REQUIRE_THROWS(arena.alloc<float>(1000));
}
