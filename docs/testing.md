# Testing

The C++ port uses [Catch2 v3](https://github.com/catchorg/Catch2) for unit testing.

Relevant files:

- [`cpp-voxel/CMakeLists.txt`](../cpp-voxel/CMakeLists.txt) — test targets
- [`cpp-voxel/tests/`](../cpp-voxel/tests/) — test source files

## How Tests Run

```bash
cd cpp-voxel
cmake -B build -S .
cmake --build build
./build/voxel_tests
```

Or use the CMake test runner:

```bash
cd cpp-voxel/build
ctest
```

## Current Test Suites (55 test cases, 572 assertions)

### `test_aabb.cpp` / `test_frustum.cpp` (Math)

Covers AABB construction and frustum plane extraction + intersection testing.

### `test_ring_buffer.cpp` / `test_scratch_arena.cpp` / `test_shared_pool.cpp` / `test_lockfree_queue.cpp` (Memory)

Covers ring buffer operations, arena allocation, shared pool acquire/release, and lock-free SPSC queue with wrap-around.

### `test_entity_manager.cpp` / `test_component_store.cpp` / `test_query.cpp` / `test_system_manager.cpp` (ECS)

Covers entity allocation/generation reuse, component SoA operations, multi-component queries, and system stage ordering.

### `test_block_registry.cpp` / `test_world.cpp` (World)

Covers block registration, chunk lifecycle, and world coordinate mapping.

### `test_simplex_noise.cpp` / `test_biome_sampler.cpp` / `test_worldgen.cpp` (World Generation)

Covers noise determinism, biome selection, bedrock placement, and seed-dependent terrain variation.

### `test_thread_pool.cpp` (Threading)

Covers concurrent job execution, result returns, and high-volume dispatch.

### `test_save_manager.cpp` (Save System)

Covers chunk file I/O and path generation.

## Adding a New Test

1. Create `tests/test_your_feature.cpp`
2. Add the file to the `voxel_tests` target in `CMakeLists.txt`
3. Build and run:

```bash
cmake --build build && ./build/voxel_tests
```
