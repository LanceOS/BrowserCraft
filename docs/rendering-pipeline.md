# Rendering Pipeline

> **C++ Port:** This engine now uses OpenGL 4.6 Core instead of WebGL2. See `cpp-terrain/src/engine/render/` for the current implementation.

This document describes how the current renderer turns chunk data into a frame on screen.

The core files are:

- [`cpp-terrain/src/game/Game.hpp`](../cpp-terrain/src/game/Game.hpp)
- [`cpp-terrain/src/engine/render/Renderer.hpp`](../cpp-terrain/src/engine/render/Renderer.hpp)
- [`cpp-terrain/src/engine/render/ChunkMesh.hpp`](../cpp-terrain/src/engine/render/ChunkMesh.hpp)
- [`cpp-terrain/src/engine/render/Texture2DArray.hpp`](../cpp-terrain/src/engine/render/Texture2DArray.hpp)
- [`cpp-terrain/src/engine/math/Frustum.hpp`](../cpp-terrain/src/engine/math/Frustum.hpp)
- [`cpp-terrain/src/engine/render/shaders/ShaderSources.hpp`](../cpp-terrain/src/engine/render/shaders/ShaderSources.hpp)
- [`cpp-terrain/src/engine/render/shaders/ShaderSources.hpp`](../cpp-terrain/src/engine/render/shaders/ShaderSources.hpp)
- (removed, time UBO handled by Renderer)

## Frame Entry Point

Rendering begins in [`Game.render()`](../cpp-terrain/src/game/Game.hpp):

1. sync UI state
2. resize the canvas to its display size
3. update camera projection and matrices
4. render terrain and sky through `Renderer.render(...)`
5. render particles
6. render the inventory HUD

The main 3D draw call path is all inside [`Renderer.render()`](../cpp-terrain/src/engine/render/Renderer.hpp).

## Renderer Responsibilities

`Renderer` owns:

- the WebGL2 context
- terrain and sky shader programs
- the camera and time UBOs
- the texture array used by chunk shading
- the frustum culler
- the GPU-side `ChunkMesh` cache keyed by chunk coordinate string

It does not own world generation, meshing, or gameplay state. It consumes completed chunk meshes from `World`.

## Texture Source (Planned Data-Driven Refactor)

> **Architecture Note:** Textures are currently generated in code via hardcoded enumerations, which violates data-driven design principles. A centralized **Asset Manager** is planned to replace this.

Currently, [`Renderer`](../cpp-terrain/src/engine/render/Renderer.hpp):

- defines a `LAYER_COLORS` map keyed by [`Tex`](../cpp-terrain/src/world/MaterialDefinition.hpp)
- synthesizes 16x16 RGBA layers with `makeLayer(...)`
- uploads every layer into a [`Texture2DArray`](../cpp-terrain/src/engine/render/Texture2DArray.hpp)
- generates mipmaps once after seeding

This means that adding a new block texture layer currently requires C++ changes. The planned refactor will migrate to a system where textures and materials are loaded from JSON/PNG files at startup, so adding a new texture requires zero code changes.

## Uniform Buffers

The renderer binds two std140 uniform blocks:

- `CameraBlock` at binding `0`
- `TimeBlock` at binding `2`

### Camera Block

`Renderer` allocates `CAMERA_BLOCK_FLOATS = 80`, which packs:

- projection matrix
- view matrix
- view-projection matrix
- inverse view-projection matrix
- camera position plus frame time
- fog color plus fog distance
- camera right vector
- camera up vector

The data is assembled in [`uploadCameraBlock()`](../cpp-terrain/src/engine/render/Renderer.hpp).

### Time Block

(Time UBO is now handled by `Renderer` directly — see [`Renderer`](../cpp-terrain/src/engine/render/Renderer.hpp).)

- elapsed time
- sun angle
- daylight factor
- effective light level
- sun direction
- padding

The time UBO is updated once per simulation tick and consumed by both the sky shader and chunk shader.

## Vertex Format

Chunk meshes use `vertexStrideFloats = 10`.

The current layout is:

1. `a_pos` as `vec3`
2. `a_normal` as `vec3`
3. `a_uv` as `vec2`
4. `a_texLayer` as `float`
5. `a_lightData` as `float`

[`ChunkMesh.upload()`](../cpp-terrain/src/engine/render/ChunkMesh.hpp) binds these as vertex attributes `0` through `4`.

`a_lightData` is stored as a float on the CPU side, then rounded back to an integer in the vertex shader and passed through as `flat uint v_packedLight`.

## What The Mesher Produces

The mesher writes directly into shared slot memory:

- `slot.vertices`
- `slot.indices`
- `slot.vertexCount`
- `slot.indexCount`

By the time `Renderer` sees a chunk in `meshReady`, the worker has already:

- computed packed light values
- packed AO into the light integer
- chosen the texture layer for each face

The renderer does not rebuild mesh topology. It only uploads the finished arrays.

## Frame Sequence

`Renderer.render(world, camera, timeSeconds, daylightFactor)` follows this sequence:

1. `syncChunks(world)` (or map persistent buffers for workers)
2. set viewport
3. clear color and depth
4. upload the camera UBO
5. draw the sky
6. bind chunk shader and block texture array
7. **Compute Shader Culling**: extract frustum planes and dispatch compute shader to cull chunks and populate the indirect draw buffer on the GPU.
8. **Multi-Draw Indirect**: issue a single `glMultiDrawElementsIndirect` to draw all visible chunks.

## Chunk Upload Path (Planned Persistent Mapping)

> **Optimization Note:** The current synchronous upload in the main thread introduces latency. A migration to **Persistent Mapped Buffers** (`glBufferStorage` with `GL_MAP_PERSISTENT_BIT`) is planned.

Currently, `syncChunks(world)` has two responsibilities:

- drop GPU meshes for chunks that no longer exist in `World`
- upload new `meshReady` chunks into `ChunkMesh` objects

Upload works like this:

1. fetch the slot views from `world.getChunkSlot(chunk)`
2. create exact subarrays from `slot.vertices` and `slot.indices`
3. upload them into the chunk's VAO/VBO/EBO wrapper using `glBufferSubData`
4. mark the chunk uploaded

In the persistent mapped architecture, worker threads will write their meshes directly into the GPU-visible memory, and `syncChunks` will simply acknowledge the new byte offsets for the compute culler.

## Frustum Culling (Planned Compute Shader)

> **Optimization Note:** CPU culling will be replaced by GPU Compute Shader culling to prepare for Multi-Draw Indirect.

Currently, `Renderer` uses the frustum culler (see [`Frustum`](../cpp-terrain/src/engine/math/Frustum.hpp)) to reject chunk AABBs before drawing.

For each chunk:

- `min = [chunkX * chunkSize, 0, chunkZ * chunkSize]`
- `max = [minX + chunkSize, worldHeight, minZ + chunkSize]`

The culler extracts six planes from the camera matrix and tests each chunk AABB conservatively on the CPU. Under the planned architecture, the CPU simply passes these planes to a compute shader, which performs this math in parallel for all chunks and fills an indirect draw buffer.

## Sky Pass

The sky uses a dedicated shader pair (see [`ShaderSources`](../cpp-terrain/src/engine/render/shaders/ShaderSources.hpp)).

The renderer creates a fullscreen triangle once at startup. Each frame it:

- disables depth writes
- binds the sky VAO
- draws three vertices
- restores depth writes

The sky fragment shader reconstructs a view ray from the inverse view-projection matrix and blends:

- day gradient
- night gradient
- dusk tint
- sun disc
- moon disc
- stars

## Chunk Shading

The chunk fragment shader combines:

- albedo from the texture array
- sky light from the packed light value
- block light from the packed light value
- AO from the high bits of the packed light value
- a small directional contribution from `u_sunDir`
- distance fog

This is why lighting changes at two levels:

- static-ish per-vertex data from the mesher
- dynamic daylight scaling from `TimeSystem`

## Camera Data Expectations

`Renderer` expects a camera view that already contains:

- projection matrix
- view matrix
- view-projection matrix
- inverse view-projection matrix
- position
- right vector
- up vector

`Renderer` consumes these values. It does not derive them internally.

## Extension Notes

If you need to change rendering data flow, these are the common touch points:

- changing vertex layout: update `SharedPool`, mesher output, `ChunkMesh.upload()`, and both chunk shaders
- adding a new texture layer: update `Tex`, `Renderer` texture seeding, and the block definition that uses it
- changing camera UBO shape: update `Renderer`, `CameraView` producers, and any shader reading `CameraBlock`
- changing time UBO shape: update `Renderer` and both shader sources in `ShaderSources.hpp`
