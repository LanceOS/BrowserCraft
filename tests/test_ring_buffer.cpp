#include <catch2/catch_test_macros.hpp>
#include "engine/alloc/RingBuffer.hpp"

TEST_CASE("RingBuffer push and shift", "[alloc]") {
  terrain::RingBuffer<int> buf(3);

  REQUIRE(buf.empty());
  REQUIRE(buf.size() == 0);

  REQUIRE(buf.push(10));
  REQUIRE(buf.size() == 1);
  REQUIRE(buf.push(20));
  REQUIRE(buf.push(30));
  REQUIRE(buf.full());

  // Should fail on overflow
  REQUIRE_FALSE(buf.push(40));

  REQUIRE(buf.shift() == 10);
  REQUIRE(buf.size() == 2);
  REQUIRE(buf.shift() == 20);
  REQUIRE(buf.shift() == 30);
  REQUIRE(buf.empty());
  REQUIRE_FALSE(buf.shift().has_value());
}

TEST_CASE("RingBuffer wraps around", "[alloc]") {
  terrain::RingBuffer<int> buf(3);
  buf.push(1);
  buf.push(2);
  buf.shift(); // remove 1
  buf.push(3);
  buf.push(4); // wraps

  REQUIRE(buf.shift() == 2);
  REQUIRE(buf.shift() == 3);
  REQUIRE(buf.shift() == 4);
}
