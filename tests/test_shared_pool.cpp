#include <catch2/catch_test_macros.hpp>
#include "engine/alloc/SharedPool.hpp"

TEST_CASE("SharedPool create and acquire", "[alloc]") {
  voxel::ChunkDimensions dims{};
  dims.sizeX = 16;
  dims.sizeY = 256;
  dims.sizeZ = 16;
  dims.maxVertsPerChunk = 10000;
  dims.maxIndicesPerChunk = 20000;
  dims.vertexStrideFloats = 8;

  auto pool = voxel::SharedPool::create(4, dims);
  REQUIRE(pool->capacity() == 4);
  REQUIRE(pool->slotByteSize() > 0);

  auto slot = pool->acquire();
  REQUIRE(slot.has_value());
  REQUIRE(*slot->status == static_cast<int32_t>(voxel::ChunkSlotStatus::FREE));
  REQUIRE(*slot->vertexCount == 0);
  REQUIRE(*slot->indexCount == 0);

  // Write voxel data and verify
  slot->voxels[0] = 5;
  REQUIRE(slot->voxels[0] == 5);
}

TEST_CASE("SharedPool acquire all then release", "[alloc]") {
  voxel::ChunkDimensions dims{};
  dims.sizeX = 16;
  dims.sizeY = 256;
  dims.sizeZ = 16;
  dims.maxVertsPerChunk = 100;
  dims.maxIndicesPerChunk = 200;
  dims.vertexStrideFloats = 8;

  auto pool = voxel::SharedPool::create(3, dims);

  auto s0 = pool->acquire();
  auto s1 = pool->acquire();
  auto s2 = pool->acquire();
  REQUIRE(s0.has_value());
  REQUIRE(s1.has_value());
  REQUIRE(s2.has_value());

  // Pool should be exhausted
  REQUIRE_FALSE(pool->acquire().has_value());

  // Release and re-acquire
  pool->release(*s0);
  auto s3 = pool->acquire();
  REQUIRE(s3.has_value());
  REQUIRE(s3->slotIndex == s0->slotIndex);
}

TEST_CASE("SharedPool multi-threaded view access", "[alloc]") {
  voxel::ChunkDimensions dims{};
  dims.sizeX = 16;
  dims.sizeY = 256;
  dims.sizeZ = 16;
  dims.maxVertsPerChunk = 100;
  dims.maxIndicesPerChunk = 200;
  dims.vertexStrideFloats = 8;

  auto pool = voxel::SharedPool::create(2, dims);

  // view() works from any context (single SharedPool, no attach needed)
  auto slot = pool->view(0);
  slot.voxels[5] = 42;
  REQUIRE(slot.voxels[5] == 42);

  // Same data visible through another view
  auto mainSlot = pool->view(0);
  REQUIRE(mainSlot.voxels[5] == 42);
}
