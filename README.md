# Voxel Sandbox Engine

This repository contains a browser-based voxel sandbox built with TypeScript, WebGL2, Web Workers, and `SharedArrayBuffer`. The current codebase is centered around a small runtime stack:

- [`src/main.ts`](src/main.ts) bootstraps the app and restarts sessions.
- [`src/game/Game.ts`](src/game/Game.ts) composes the world, renderer, ECS stores, UI, save system, and workers.
- [`src/world/World.ts`](src/world/World.ts) owns chunk visibility, generation, meshing, save/load hooks, and unloads.
- [`src/engine/render/Renderer.ts`](src/engine/render/Renderer.ts) handles GPU uploads, sky rendering, frustum culling, and terrain draws.
- [`src/engine/alloc/SharedPool.ts`](src/engine/alloc/SharedPool.ts) provides the shared chunk memory used across the main thread and workers.

## Quick Start

1. Make sure you have a recent Node.js release and a working `tsc` on your `PATH`.
2. Build the project:

```bash
npm run build
```

3. Serve it with the included static server:

```bash
npm run serve
```

4. Open `http://localhost:4173`.

You can also run both steps with:

```bash
npm run start
```

## Important Runtime Requirement

This project depends on `SharedArrayBuffer`, which means it must run with cross-origin isolation headers. The included server in [`scripts/serve.mjs`](scripts/serve.mjs) sets:

- `Cross-Origin-Embedder-Policy: require-corp`
- `Cross-Origin-Opener-Policy: same-origin`

Opening `index.html` directly from disk will not work.

## Scripts

- `npm run build`: compile `src/` to `dist/` using `tsc`
- `npm run check`: type-check without emitting files
- `npm run test`: build first, then run the Node-based test suite under `tests/`
- `npm run serve`: serve the repo root with the required COOP/COEP headers
- `npm run start`: build, then serve

Note: `package.json` currently defines scripts only. It does not declare a local TypeScript dependency, so `tsc` must already be available in your environment.

## Project Layout

```text
src/
  content/   Biomes, crafting data, mobs, structures
  engine/    Core loop, allocators, ECS, math, render, save, workers
  game/      Composition root, player setup, interaction controllers
  ui/        Menu and inventory HUD
  world/     Chunk and block orchestration
tests/       Node-based tests against built output in dist/
docs/        Architecture notes, subsystem design docs, and contributor guides
dist/        Compiled JavaScript output consumed by index.html
```

## How The Runtime Fits Together

- [`src/main.ts`](src/main.ts) creates the canvas and starts a [`Game`](src/game/Game.ts) session.
- [`Game`](src/game/Game.ts) creates the ECS stores, renderer, world, UI, save worker, and the worldgen/mesher worker pools.
- [`World`](src/world/World.ts) decides which chunks should exist around the camera and moves them through load, generate, mesh, upload, and unload phases.
- Workers operate on shared chunk slots backed by [`SharedPool`](src/engine/alloc/SharedPool.ts), avoiding repeated copies of voxel and mesh data.
- [`Renderer`](src/engine/render/Renderer.ts) uploads finished chunk meshes to GPU buffers and renders the sky plus visible chunk meshes each frame.
- [`SaveManager`](src/engine/save/SaveManager.ts) and [`SaveWorker`](src/engine/workers/SaveWorker.ts) serialize chunk state into IndexedDB without blocking the main thread.

## Docs

Start with the docs below if you want the current implementation instead of older design sketches:

- [`docs/README.md`](docs/README.md)
- [`docs/chunk-and-worker-lifecycle.md`](docs/chunk-and-worker-lifecycle.md)
- [`docs/rendering-pipeline.md`](docs/rendering-pipeline.md)
- [`docs/input-ui-inventory-flow.md`](docs/input-ui-inventory-flow.md)
- [`docs/testing.md`](docs/testing.md)
- [`docs/content-extension-guide.md`](docs/content-extension-guide.md)

Some older files in `docs/` are still useful background, but many of them were written as subsystem design documents before the current implementation settled. When in doubt, treat the source as authoritative.
