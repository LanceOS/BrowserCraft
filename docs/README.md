# Docs Index

> **Note:** The engine has been converted from TypeScript to C++.
> The C++ source lives in [`../cpp-voxel/src/`](../cpp-voxel/src/).
> All documentation below references the original TypeScript for design concepts;
> see the C++ headers and source files in `cpp-voxel/src/` for the current implementation.

This directory now contains two kinds of documentation:

- current implementation docs, which are written against the code that exists today
- older subsystem design docs, which are still useful background but may describe earlier plans or partially outdated details

If you are trying to understand or extend the repo, start with the current implementation docs first.

## Start Here

- [`../cpp-voxel/README.md`](../cpp-voxel/README.md): C++ project overview, build instructions, and architecture
- [`chunk-and-worker-lifecycle.md`](chunk-and-worker-lifecycle.md): chunk state flow across `World`, `SharedPool`, workers, save/load, and renderer upload
- [`rendering-pipeline.md`](rendering-pipeline.md): camera/time UBOs, texture array setup, chunk upload, sky pass, and frustum culling
- [`input-ui-inventory-flow.md`](input-ui-inventory-flow.md): controls, pointer lock, menu state, inventory, crafting, and HUD behavior
- [`testing.md`](testing.md): how tests run and what the current suites cover
- [`content-extension-guide.md`](content-extension-guide.md): how to add blocks, recipes, structures, biomes, and mobs in the current codebase

## Current Implementation Notes

These docs are intended to stay in sync with the source:

- [`chunk-and-worker-lifecycle.md`](chunk-and-worker-lifecycle.md)
- [`rendering-pipeline.md`](rendering-pipeline.md)
- [`input-ui-inventory-flow.md`](input-ui-inventory-flow.md)
- [`testing.md`](testing.md)
- [`content-extension-guide.md`](content-extension-guide.md)

## Older Subsystem Design Docs

These files are still valuable for rationale, algorithms, and intended constraints, but validate them against code before making changes:

- [`instructions.md`](instructions.md)
- [`worldgeneration.md`](worldgeneration.md)
- [`structures.md`](structures.md)
- [`chunkSerializationAndSaveAndLoad.md`](chunkSerializationAndSaveAndLoad.md)
- [`lighting.md`](lighting.md)
- [`dayAndNightCycle.md`](dayAndNightCycle.md)
- [`controls.md`](controls.md)
- [`menu.md`](menu.md)
- [`craftingAndInventory.md`](craftingAndInventory.md)
- [`particleSystem.md`](particleSystem.md)
- [`audioEngine.md`](audioEngine.md)
- [`physicsAndCollisionDetection.md`](physicsAndCollisionDetection.md)
- [`mobsAndBlocks.md`](mobsAndBlocks.md)
- [`redstone.md`](redstone.md)
- [`optimization.md`](optimization.md)

## Updating Docs

When adding or revising docs:

- prefer linking to real source files instead of describing concepts in isolation
- call out places where the current implementation intentionally differs from older design notes
- keep the source authoritative if a design note and the code disagree
