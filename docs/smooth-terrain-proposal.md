# Smooth Terrain Proposal

> Status: proposed architecture with a Surface Nets prototype in code

This document proposes a smooth-terrain system for the current C++ engine.
The goal is to make natural terrain render as a smooth triangle surface while
preserving the existing voxel/block layer for placed blocks and other discrete
gameplay systems.

A first implementation prototype already exists in:

- [`src/game/ChunkWorkerImpl.cpp`](../src/game/ChunkWorkerImpl.cpp)
- [`src/world/mesh/SurfaceNetsMesher.hpp`](../src/world/mesh/SurfaceNetsMesher.hpp)
- [`src/world/mesh/SurfaceNetsMesher.cpp`](../src/world/mesh/SurfaceNetsMesher.cpp)

It is gated by `GameConfig::useSurfaceNets` and still feeds the current chunk
upload path; the broader terrain renderer, collision, and persistence split
described below remain future work.

The intended result is:

- smooth hills, cliffs, cave mouths, and overhangs
- procedural terrain that keeps the same world-generation character
- future terrain damage that reconnects cleanly to nearby surfaces after a remesh
- minimal disruption to the current block placement and chunk streaming model

## Why This Is Needed

The current engine already blends biome height smoothly in generation, but it
quantizes the terrain into block ids before meshing:

- terrain generation writes block ids in [`src/world/generation/WorldGenPipeline.cpp`](../src/world/generation/WorldGenPipeline.cpp)
- the chunk mesher emits axis-aligned cube faces in [`src/engine/workers/mesher/GreedyMesher.cpp`](../src/engine/workers/mesher/GreedyMesher.cpp)
- world queries like solidity and raycast assume integer block cells in
  [`src/world/VoxelStore.cpp`](../src/world/VoxelStore.cpp),
  [`src/engine/collision/EntityCollisions.cpp`](../src/engine/collision/EntityCollisions.cpp),
  and [`src/world/BlockRaycast.hpp`](../src/world/BlockRaycast.hpp)

This means the sharp, stepped terrain silhouette is primarily a representation
and meshing issue, not a noise-generation issue.

## Goals

- Render natural terrain as a smooth triangle mesh instead of exposed cube faces.
- Preserve the current terrain noise stack and biome logic where possible.
- Support future digging and deformation with automatic surface reconnection.
- Reuse the current chunk lifecycle, worker ownership, and slot-based streaming.
- Keep placed blocks, flora, and similar gameplay on the current voxel path in v1.

## Non-Goals

- Full micro-voxel storage for the entire world
- Replacing every block-based system in the first milestone
- Folding smooth terrain into the existing `top/bottom/side` block texture model
- Immediate support for redstone, structures, and block placement inside smooth terrain

## Current Constraints In The Engine

The current codebase is built around a discrete block grid:

- chunk generation and meshing are dispatched through [`src/world/IChunkWorker.hpp`](../src/world/IChunkWorker.hpp)
  and implemented in [`src/game/ChunkWorkerImpl.cpp`](../src/game/ChunkWorkerImpl.cpp)
- chunk completion and lifecycle state are processed by
  [`src/game/WorldController.cpp`](../src/game/WorldController.cpp)
- chunk data ownership lives in [`src/engine/alloc/SharedPool.cpp`](../src/engine/alloc/SharedPool.cpp)
- uploaded draw metadata is published through [`src/engine/render/ChunkSyncer.cpp`](../src/engine/render/ChunkSyncer.cpp)

The smooth-terrain design should fit those seams instead of replacing them all
at once.

## Proposed High-Level Model

Introduce a second terrain representation for natural ground:

1. Keep the existing voxel/block layer for placed blocks and discrete gameplay.
2. Add a new density-field layer for natural terrain only.
3. Mesh the density field into smooth triangles using an isosurface algorithm.
4. Render terrain and block geometry as separate mesh paths.

This is a hybrid system:

- natural terrain uses a smooth field
- player-placed cubes remain cubes

That lets the world look smooth without discarding the existing block gameplay
architecture.

## Proposed Terrain Data Model

Add a `TerrainPool` parallel to the current `SharedPool`, keyed by the same
`slotIndex` used by `Chunk`.

Suggested per-slot data:

- `density`: quantized signed field samples, stored as `int16_t`
- `revision`: increments when terrain data changes
- `flags`: dirty-field, dirty-mesh, edited, has-surface
- `chunkX`, `chunkZ`: mirrored for convenience and validation

### Sample Lattice

For a chunk of `chunkSize x worldHeight x chunkSize`, store terrain samples on a
lattice of:

- `(chunkSize + 1) x (worldHeight + 1) x (chunkSize + 1)`

With the current defaults of `16 x 256 x 16`, that is:

- current voxel cells: `16 x 256 x 16 = 65,536`
- terrain lattice samples: `17 x 257 x 17 = 74,273`

This is a modest increase compared with full micro-voxel subdivision and is one
of the main reasons a field-based terrain layer is preferred.

### Storage Type

Use `int16_t` for density in v1:

- smaller than `float`
- deterministic for save/load and border checks
- adequate precision for terrain surfaces

Material data should not be stored per sample in the first milestone. Instead,
derive terrain material hints from world-space rules during meshing or shading.

## World Generation Changes

Refactor the current logic in
[`src/world/generation/WorldGenPipeline.cpp`](../src/world/generation/WorldGenPipeline.cpp)
into continuous field sampling instead of ending at an integer `surfaceY`.

### Proposed Sampling Functions

- `sampleSurfaceHeight(worldX, worldZ) -> float`
- `sampleDensity(worldX, worldY, worldZ) -> float`
- `sampleTerrainMaterial(worldX, worldY, worldZ) -> TerrainMaterial`

### Density Construction

The terrain field should reuse the current noise stack:

- continental noise
- detail noise
- biome blended height bias
- mountain amplification
- cave/density noise

Suggested base rule:

- positive density = solid terrain
- negative density = air
- zero crossing = terrain surface

A simple starting point is:

- `baseDensity = surfaceHeight - worldY`

Then apply cave subtraction and any local shaping modifiers.

### Chunk Seam Rule

All density samples must be generated in world coordinates, not local chunk
coordinates. Shared border samples must be bit-identical between neighboring
chunks. This is required to prevent cracks.

## Meshing Strategy

Use `Surface Nets` for the first implementation.

Why `Surface Nets` first:

- cleaner terrain output than greedy voxel faces
- lighter and simpler than marching cubes for a first milestone
- good fit for hills, cliffs, and caves
- easier to stabilize for editing than hand-authored cube subdivision

### Surface Nets Overview

For each cell in the density lattice:

1. read the 8 corner densities
2. detect whether the isosurface crosses the cell
3. compute edge intersections
4. place one surface vertex inside the active cell
5. connect neighboring active cells into quads or triangle pairs

The current C++ prototype follows this shape directly: the chunk worker passes
a `sampleDensity` callback into the Surface Nets mesher, which emits vertex and
index buffers plus per-vertex normals on the same mesh layout used by the rest
of the renderer.

### Normals

Surface normals should be derived from the density gradient using central
differences on the lattice, not from flat triangle normals alone.

### Chunk Border Rule

Sample borders are shared, but surface cells are owned by only one chunk:

- the chunk may sample border points on all sides
- it should only emit cells in its local owned range

This avoids duplicate geometry while keeping seams closed.

## Rendering Plan

The long-term target is still a separate terrain render path:

- `TerrainMeshAllocator`
- `TerrainChunkSyncer`
- terrain shader and terrain material inputs

The current prototype keeps the Surface Nets branch inside
[`src/game/ChunkWorkerImpl.cpp`](../src/game/ChunkWorkerImpl.cpp) and publishes
through the existing chunk mesh allocator and syncer so we can validate density
sampling, seam handling, and buffer budgets before splitting the render path.
The existing allocator and syncer in
[`src/engine/render/ChunkSyncer.cpp`](../src/engine/render/ChunkSyncer.cpp)
still assume block-oriented mesh metadata and block shader behavior, so that
separate terrain path should land before this graduates from prototype to
production terrain.

## Terrain Materials

Smooth terrain should not rely on the current block face texture model in
[`src/world/BlockDefinition.hpp`](../src/world/BlockDefinition.hpp).

Recommended v1 terrain shader features:

- triplanar texturing
- slope-aware blending
- height/depth-based material rules
- per-vertex normal lighting

Suggested terrain materials:

- grass
- dirt
- stone
- sand
- mud or swamp soil
- snow or high-altitude variant

These can be selected by biome, slope, height, and depth below the surface.

## Editing Model

Terrain deformation should be brush-based, not single-block based.

Recommended edit operations:

- add sphere
- subtract sphere
- smooth
- flatten

Each edit:

1. modifies nearby density samples
2. increments the terrain revision
3. marks the chunk mesh dirty
4. remeshes the owning chunk and any border-sharing neighbors

This is the mechanism that allows damaged terrain to reconnect to the next
surface automatically after a remesh.

## Collision And Raycast

Do not replace the current block-based queries immediately.

Phase the interaction model:

- keep current block collision and block raycast for placed blocks
- later add terrain collision and terrain raycast against the terrain mesh

Relevant current files:

- [`src/engine/collision/EntityCollisions.cpp`](../src/engine/collision/EntityCollisions.cpp)
- [`src/engine/collision/EntityCollisionsMovement.cpp`](../src/engine/collision/EntityCollisionsMovement.cpp)
- [`src/world/BlockRaycast.hpp`](../src/world/BlockRaycast.hpp)

In a later phase, gameplay queries can combine:

- terrain mesh hit or collision result
- block-layer hit or collision result

## Persistence Strategy

The current save format writes raw block arrays in
[`src/engine/save/SaveManager.cpp`](../src/engine/save/SaveManager.cpp).

Smooth terrain should not save the full regenerated field by default. Prefer:

- sparse terrain edits
- brush operation history
- or density deltas for edited regions only

Untouched terrain should continue to regenerate from the world seed.

## Proposed Milestones

### Milestone 0: Field Sampler Extraction

- refactor the current worldgen formulas into reusable continuous functions
- keep current block terrain rendering unchanged

### Milestone 1: Render-Only Prototype

- add `TerrainPool`
- sample terrain density per chunk
- build smooth terrain mesh with `Surface Nets`
- render it with a dedicated terrain shader
- no digging, no terrain collision, no save changes yet

### Milestone 2: Terrain Material Pass

- add triplanar or blended terrain materials
- validate visual transitions across biomes and slopes

### Milestone 3: Terrain Editing

- add subtractive and additive terrain brushes
- remesh touched chunks and border neighbors

### Milestone 4: Terrain Collision And Raycast

- add terrain-aware collision tests
- add terrain raycast for digging and targeting

### Milestone 5: Persistence

- save only terrain edits or deltas
- regenerate untouched terrain from seed

### Milestone 6: Block/Terrain Integration

- define placement rules for cubes on smooth surfaces
- define overlap or replacement rules between terrain and block geometry

## Current Prototype

A first slice is already in the tree:

- `GameConfig::useSurfaceNets` toggles the branch in
  [`src/game/ChunkWorkerImpl.cpp`](../src/game/ChunkWorkerImpl.cpp)
- [`src/world/mesh/SurfaceNetsMesher.hpp`](../src/world/mesh/SurfaceNetsMesher.hpp)
  and [`src/world/mesh/SurfaceNetsMesher.cpp`](../src/world/mesh/SurfaceNetsMesher.cpp)
  extract the isosurface from the scalar density field
- [`src/engine/render/ChunkSyncer.cpp`](../src/engine/render/ChunkSyncer.cpp)
  still uploads the result through the current chunk path, with a slightly
  larger border pad for smooth meshes

The separate terrain renderer, material stack, collision, and persistence work
below are still future milestones.

## Risks And Design Watchpoints

- cracks at chunk borders if shared samples are not identical
- duplicate geometry if border cells are emitted by both chunks
- awkward seams where voxel blocks meet smooth terrain
- collision complexity once players can stand on terrain triangles
- shader/material complexity compared with the current block face model
- terrain edits that affect save size or remesh cost more than expected

## Recommended First Deliverable

The best first implementation target is:

- deterministic terrain field sampling
- a render-only `Surface Nets` terrain mesh
- a separate terrain material shader
- no gameplay edits or collision rewrites yet

That is the fastest way to validate the visual goal before paying the full cost
of gameplay and persistence changes.

## Summary

This proposal keeps the existing voxel architecture where it is strongest and
adds a dedicated smooth-terrain layer for natural ground.

That approach is preferred over cube subdivision because:

- it actually changes the terrain silhouette
- it scales much better than micro-voxel storage
- it supports future terrain deformation that reconnects to neighboring surfaces
- it fits the current chunk, worker, and slot-based ownership model
