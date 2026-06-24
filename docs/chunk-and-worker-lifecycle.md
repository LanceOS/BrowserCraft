# Chunk And Worker Lifecycle

This document describes how a chunk moves through the current engine from visibility discovery to GPU upload and eventual unload.

The main moving pieces are:

- [`cpp-voxel/src/game/Game.hpp`](../cpp-voxel/src/game/Game.hpp)
- [`cpp-voxel/src/world/World.hpp`](../cpp-voxel/src/world/World.hpp)
- [`cpp-voxel/src/world/Chunk.hpp`](../cpp-voxel/src/world/Chunk.hpp)
- [`cpp-voxel/src/engine/alloc/SharedPool.hpp`](../cpp-voxel/src/engine/alloc/SharedPool.hpp)
- [`cpp-voxel/src/engine/threading/WorkerThreadPool.hpp`](../cpp-voxel/src/engine/threading/WorkerThreadPool.hpp)
- [`cpp-voxel/src/engine/workers/worldgen/WorldGenPipeline.hpp`](../cpp-voxel/src/engine/workers/worldgen/WorldGenPipeline.hpp)
- [`cpp-voxel/src/engine/render/ChunkMesh.hpp`](../cpp-voxel/src/engine/render/ChunkMesh.hpp)
- [`cpp-voxel/src/engine/save/SaveManager.hpp`](../cpp-voxel/src/engine/save/SaveManager.hpp)
- [`cpp-voxel/src/engine/save/SaveManager.hpp`](../cpp-voxel/src/engine/save/SaveManager.hpp)
- [`cpp-voxel/src/engine/render/Renderer.hpp`](../cpp-voxel/src/engine/render/Renderer.hpp)

## High-Level Flow

1. The game creates a shared chunk pool and starts worker pools.
2. `World.update()` decides which chunks should exist around the camera.
3. Each new chunk either loads from disk or queues for procedural generation.
4. Generated or loaded voxel data is queued for meshing.
5. The mesher computes lighting, builds vertex/index data, and marks the chunk mesh-ready.
6. The renderer uploads finished buffers to the GPU and marks the chunk uploaded.
7. Chunks that move outside the visible radius are released back to the shared pool.

## Shared Chunk Memory

Every chunk lives in one slot of [`SharedPool`](../cpp-voxel/src/engine/alloc/SharedPool.hpp). A slot contains:

- a small atomic header
- `voxels`
- `light`
- `redstone`
- `vertices`
- `indices`

This matters because the workers do not own separate copies of chunk data. The main thread, worldgen worker, and mesher worker all attach typed-array views onto the same `SharedArrayBuffer`.

### Slot Header

The slot header exposes:

- `status`
- `vertexCount`
- `indexCount`
- `chunkX`
- `chunkZ`
- `genSeed`

The main thread owns allocation and release. Worker-attached pool views can inspect and mutate slot contents, but they cannot acquire or release slots.

## Two Layers Of State

The code tracks chunk progress in two places:

- [`Chunk.state`](../cpp-voxel/src/world/Chunk.hpp): a main-thread-friendly string state used by `World`
- [`ChunkSlotStatus`](../cpp-voxel/src/engine/alloc/SharedPool.hpp): an atomic enum stored inside shared memory

### `Chunk.state`

Current string states are:

- `loadingFromDisk`
- `queuedGen`
- `generating`
- `voxelsReady`
- `queuedMesh`
- `meshing`
- `meshReady`
- `uploaded`
- `meshFailed`

### `ChunkSlotStatus`

The atomic slot enum is:

- `FREE`
- `GENERATING`
- `VOXELS_READY`
- `MESHING`
- `MESH_READY`
- `GPU_UPLOADED`

The string state is easier for gameplay code to reason about. The atomic status is what workers use to avoid acting on stale slots.

## Startup

During construction, [`Game`](../cpp-voxel/src/game/Game.hpp):

- creates a [`SharedPool`](../cpp-voxel/src/engine/alloc/SharedPool.hpp)
- spawns worldgen workers and mesher workers through [`WorkerThreadPool`](../cpp-voxel/src/engine/threading/WorkerThreadPool.hpp)
- creates a [`World`](../cpp-voxel/src/world/World.hpp)
- creates a [`SaveManager`](../cpp-voxel/src/engine/save/SaveManager.hpp)

Both worker pools receive one `init` message containing:

- the shared pool bootstrap data
- the world seed

After that, workers only receive tiny job messages like `generate` or `mesh`.

## Step 1: Visibility Discovery

Each frame, `Game.update()` eventually calls `world.update(cameraPosition)`.

`World.update()` does three jobs:

- `ensureVisibleRadius()` creates any missing chunks within render distance
- `unloadFarChunks()` releases chunks that are no longer needed
- `pumpQueues()` gives queued jobs to idle workers

When `ensureVisibleRadius()` finds a missing chunk:

1. it acquires a slot from `SharedPool`
2. it creates a [`Chunk`](../cpp-voxel/src/world/Chunk.hpp) with `(chunkX, chunkZ, slotIndex)`
3. it writes `chunkX` and `chunkZ` into the shared slot header
4. it either requests a disk load or queues worldgen

The choice depends on whether a save manager is attached.

## Step 2A: Load From Disk

If saving is enabled, newly discovered chunks start in `loadingFromDisk`.

[`SaveManager.requestLoad()`](../cpp-voxel/src/engine/save/SaveManager.hpp) sends a `LOAD_CHUNK` message to the save worker. The worker:

- opens IndexedDB
- looks up the world/region record
- decompresses the stored chunk if present
- posts `LOAD_SUCCESS` or `LOAD_FAILED`

Back on the main thread:

- `LOAD_SUCCESS` calls `World.onSaveLoadSuccess()`
- voxel, light, and redstone arrays are copied into the slot
- the slot status becomes `VOXELS_READY`
- the chunk moves to `voxelsReady`
- the chunk is pushed onto `pendingMesh`

If the load misses, `World.onSaveLoadFailed()` pushes the chunk onto `pendingGen` instead.

## Step 2B: Procedural Generation

If a chunk is not loaded from disk, it goes through worldgen.

`pumpQueues()` looks for idle worldgen workers. For each idle worker:

1. pop one chunk from `pendingGen`
2. set slot status to `GENERATING`
3. set `chunk.state = "generating"`
4. send a `generate` message with `slotIndex`, `chunkX`, `chunkZ`, and a chunk-local seed

Inside [`WorldGenPipeline`](../cpp-voxel/src/engine/workers/worldgen/WorldGenPipeline.hpp), the pipeline:

- samples biome and density noise
- fills the voxel array
- carves caves
- places structures
- distributes ore
- stores `VOXELS_READY` in the slot header

When finished, the worker posts `generated`.

`World.onWorldGenDone()` then:

- marks the worker idle
- sets `chunk.state = "voxelsReady"`
- clears `needsRemesh`
- marks the chunk dirty for saving
- pushes the chunk onto `pendingMesh`

## Step 3: Meshing

`pumpQueues()` also looks for idle mesher workers. For each idle worker:

1. pop one chunk from `pendingMesh`
2. set slot status to `MESHING`
3. set `chunk.state = "meshing"`
4. send a `mesh` message with `slotIndex`

Inside the mesher (see [`ChunkMesh`](../cpp-voxel/src/engine/render/ChunkMesh.hpp)):

1. lighting is recalculated into the slot's `light` array
2. redstone state is consulted for dynamic emitters like the redstone lamp
3. `greedyMeshChunk()` writes interleaved vertex data and indices into the shared slot
4. the worker stores `MESH_READY` into the atomic status
5. the worker posts `meshed` with `success`, `vertexCount`, and `indexCount`

On the main thread, `World.onMeshDone()`:

- marks the worker idle
- stores the counts on the `Chunk`
- sets `chunk.state` to `meshReady` or `meshFailed`
- requeues the chunk if a remesh was requested mid-job

## Step 4: GPU Upload

The renderer performs uploads lazily inside [`Renderer.syncChunks()`](../cpp-voxel/src/engine/render/Renderer.hpp).

For each chunk in `meshReady`:

- grab the slot views from `World.getChunkSlot()`
- slice the exact vertex and index ranges
- upload them into a [`ChunkMesh`](../cpp-voxel/src/engine/render/ChunkMesh.hpp)
- call `world.markUploaded(chunk)`

`markUploaded()` sets:

- `chunk.state = "uploaded"`
- `slot.status = GPU_UPLOADED`

After this point, the slot still owns the authoritative voxel, light, redstone, and mesh arrays, but the renderer also has a GPU copy.

## Remeshing After Edits

Gameplay edits happen through:

- [`World.setBlockIdAt()`](../cpp-voxel/src/world/World.hpp)
- [`World.setRedstonePackedAt()`](../cpp-voxel/src/world/World.hpp)

Both methods:

- mutate the shared slot
- mark the chunk dirty for saving
- request a remesh

If the chunk is already meshing, `World.requestRemesh()` sets `chunk.needsRemesh = true` instead of interrupting the current worker. Once `onMeshDone()` fires, the chunk is queued again.

This behavior is covered by [`tests/world-remesh.test.mjs`](../tests/world-remesh.test.mjs) (original TS) and the C++ tests in `cpp-voxel/tests/`.

## Save Cycle

Saving is intentionally decoupled from generation and meshing.

[`SaveManager`](../cpp-voxel/src/engine/save/SaveManager.hpp):

- keeps a `dirtyQueue`
- waits for `saveInterval`
- serializes ready chunks with [`serializeChunkData()`](../cpp-voxel/src/engine/save/SaveManager.hpp)
- transfers the resulting `ArrayBuffer` to `SaveWorker`

The serialized payload includes:

- voxels
- light
- redstone

`SaveWorker` compresses the buffer with RLE and stores it under a region record in IndexedDB.

## Unload And Release

`World.unloadFarChunks()` releases chunks that have moved outside `renderDistance + 1`, but only when the chunk is in a releasable state:

- `meshReady`
- `uploaded`
- `meshFailed`

For each releasable chunk:

1. remove it from `slotToChunk`
2. remove it from the chunk manager
3. call `SharedPool.release()`

Releasing the slot returns it to the main-thread free list and marks it `FREE`.

## Worker Messages

Worker protocol currently lives in [`cpp-voxel/src/engine/threading/WorkerThreadPool.hpp`](../cpp-voxel/src/engine/threading/WorkerThreadPool.hpp).

Inbound messages:

- `init`
- `generate`
- `mesh`

Outbound messages:

- `generated`
- `meshed`

The save worker uses its own message shapes (see [`SaveManager`](../cpp-voxel/src/engine/save/SaveManager.hpp)).

## Practical Gotchas

- `Chunk.state` and `ChunkSlotStatus` are related but not identical; if they disagree, trust the code path that owns the transition.
- Render distance changes are applied by mutating `config.renderDistance` from the current session each update.
- The mesher recalculates lighting every mesh job, so edits that change visibility or emission do not need a separate light rebuild pass.
- Redstone is stored in its own array, not embedded in voxel IDs or light bytes.
