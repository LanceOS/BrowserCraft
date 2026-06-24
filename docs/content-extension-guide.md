# Content Extension Guide

This guide explains how to extend the content that exists in the current codebase. It focuses on the actual registration points that are wired today.

The main extension surfaces are:

- blocks via [`src/world/BlockFactory.ts`](../src/world/BlockFactory.ts)
- recipes via [`src/content/crafting/CraftingRegistry.ts`](../src/content/crafting/CraftingRegistry.ts)
- structures via [`src/content/structures/StructureRegistry.ts`](../src/content/structures/StructureRegistry.ts)
- biomes via [`src/engine/workers/worldgen/BiomeSampler.ts`](../src/engine/workers/worldgen/BiomeSampler.ts)
- mobs via [`src/content/mobs/MobFactory.ts`](../src/content/mobs/MobFactory.ts)

## Before You Add Content

The codebase has a few places where "registry" files exist but are not all equally authoritative yet.

Important examples:

- blocks are truly driven from `VanillaBlockFactory`
- recipes are truly driven from `createDefaultCraftingRegistry()`
- structures are truly driven from `StructureFactory`
- biomes are currently chosen inside `BiomeSampler`, not through `BiomeRegistry`

If you add new content, update the code path that the runtime actually uses.

## Adding A Block

Current block definitions live in [`VanillaBlockFactory`](../src/world/BlockFactory.ts), which registers `BlockDefinition` objects into [`BlockRegistry`](../src/world/BlockRegistry.ts).

### Block Definition Shape

A block definition needs:

- numeric `id`
- string `name`
- `textures` with `top`, `bottom`, and `side`
- `material`
- `collision`

The data shape lives in [`src/world/blocks/BlockDefinition.ts`](../src/world/blocks/BlockDefinition.ts).

### Steps

1. Pick a new block ID that does not collide with an existing registration in `VanillaBlockFactory`.
2. Add or reuse a texture layer in [`src/world/blocks/TextureLayers.ts`](../src/world/blocks/TextureLayers.ts).
3. Add a matching texture source in [`Renderer`](../src/engine/render/Renderer.ts) if the new texture layer is brand new.
4. Register the block in `VanillaBlockFactory`.
5. Set the right material flags:
   - `opaque`
   - `transparent`
   - `liquid`
   - `foliage`
   - `lightEmission`
6. Set the collision AABB:
   - use `FULL_BLOCK_AABB` for normal solids
   - use an empty AABB for non-solid logic blocks or fluids
   - provide a custom AABB for partial blocks

### Why Material Flags Matter

The material flags affect more than rendering:

- transparent/liquid/foliage blocks are treated as passable for certain lighting and meshing decisions
- `lightEmission` is consumed by the mesher's lighting pass
- collision AABB changes physics behavior through world solidity checks

### Current Caveat

External texture assets are not wired yet. Texture layers are currently synthesized in code.

## Adding A Recipe

Recipes are registered in [`createDefaultCraftingRegistry()`](../src/content/crafting/CraftingRegistry.ts).

There are two recipe types:

- `registerShapeless(...)`
- `registerShaped(...)`

### Shapeless Recipe Example

Use when only the set of ingredients matters:

- `id`
- `ingredients`
- `output`

### Shaped Recipe Example

Use when layout matters:

- `id`
- `pattern`
- `key`
- `output`
- `gridWidth`
- `gridHeight`

### Current Crafting Rules

The player crafting grid is 2x2, stored in inventory slots `40..43`, with output at slot `44`.

`CraftingSystem` currently:

- checks shapeless recipes first
- normalizes the occupied grid bounds
- checks shaped recipes
- also checks mirrored shaped recipes

If you add a recipe, add or update tests in:

- [`tests/crafting-system.test.mjs`](../tests/crafting-system.test.mjs)

## Adding A Structure

Structures are currently created inside [`StructureFactory`](../src/content/structures/StructureRegistry.ts).

The runtime path is:

1. `WorldGenWorker` creates a `StructureFactory`
2. `StructureFactory.getAll()` feeds `VillagePlanner`
3. `VillagePlanner` chooses and stamps structures into chunk voxel data

### Current Blueprint Format

Each structure is created from raw packed block tuples:

- `x`
- `y`
- `z`
- `blockId`

`createBlueprint(...)` converts the raw block list into a packed `Uint8Array` and appends a terminator sequence.

### Steps

1. Add a new registration method in `StructureFactory`.
2. Build a `rawBlocks` array using repeated `(x, y, z, blockId)` entries.
3. Call `createBlueprint(id, sizeX, sizeY, sizeZ, rawBlocks)`.
4. Register the blueprint in `this.blueprints`.
5. If needed, update the planner logic in [`VillagePlanner`](../src/content/structures/VillagePlanner.ts) so the new blueprint is selected under the right conditions.

### Current Caveat

There is no external structure file format yet. Structures are still hard-coded in TypeScript.

## Adding A Biome

Biomes need extra care because there are two biome-related surfaces in the repo:

- [`src/content/biomes/BiomeRegistry.ts`](../src/content/biomes/BiomeRegistry.ts)
- [`src/engine/workers/worldgen/BiomeSampler.ts`](../src/engine/workers/worldgen/BiomeSampler.ts)

The current worldgen pipeline actually uses `BiomeSampler`, not `BiomeRegistry`.

### To Affect World Generation Today

You need to update `BiomeSampler`:

1. add a new `BiomeId`
2. add a new entry to `BIOME_RULES`
3. update `sampleBiome(...)` thresholds so the biome can be selected

### To Keep Content Files In Sync

If you want the content-side biome definitions to stay useful too:

1. add a new biome module under `src/content/biomes/`
2. update `BiomeRegistry`

### Current Caveat

The content-side biome registry is not currently the source of truth for world generation. If you only update `BiomeRegistry`, the worldgen worker will not start using the new biome.

## Adding A Mob

Mob creation is centralized in [`MobFactory`](../src/content/mobs/MobFactory.ts).

The current system is data-driven through the `MOB_CONFIGS` table and component writes. There are no per-mob classes yet.

### Steps

1. Add a new enum member to `MobType`.
2. Add a matching `MobConfig` entry to `MOB_CONFIGS`.
3. Set:
   - width
   - height
   - eye height
   - move speed
   - attack damage
   - max health
   - model ID
   - hostile/friendly flag
4. Spawn the mob from the appropriate gameplay entry point, currently `Game` for the starter mobs.

### What `MobFactory.spawn(...)` Writes

Spawning a mob currently populates:

- transform
- rigid body
- mob stats
- health
- AI state
- hostile or friendly tag
- audio emitter

If you add a new gameplay concept that mobs need, update both the component definition and the factory population step.

## Extending Creative Inventory

If you add content that should be easy to test in-game, seed it into creative mode through [`Game.seedCreativeInventory()`](../src/game/Game.ts).

That method currently populates hotbar slots with starter blocks and items when a creative session starts.

## Recommended Validation Checklist

When you add new content:

1. confirm the numeric ID does not collide with existing data
2. confirm the content is reachable from the actual runtime path
3. add a focused test if the feature has pure logic
4. if the feature affects rendering, make sure every referenced texture layer exists
5. if the feature affects lighting or collision, verify the material flags and AABB are correct

## Good Follow-Up Work

The current content system is already usable, but these areas would make future additions easier:

- move hard-coded texture generation into a dedicated asset pipeline
- unify biome selection around one source of truth
- move structure data into an external format
- add item/block name registries for UI instead of showing raw numeric IDs
