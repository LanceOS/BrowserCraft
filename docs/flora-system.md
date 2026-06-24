# Voxel Engine Technical Design Document: Flora System

**Version:** 1.0  
**Scope:** Flora classification, abstract factories, worldgen spawning, player farming, growth mechanics, and rendering strategies for all plant life.  
**Architecture Constraints:** Strict TypeScript, Web Worker isolation, Zero-Copy SharedArrayBuffer, GLSL 300 ES, Data-Oriented Design.

---

## 1. System Overview

Flora encompasses all plant-like blocks in the world: trees, flowers, grass, crops, vines, cacti, and sugar cane. Each category shares common traits (light needs, soil affinity, growth stages) but differs in structure, collision, and rendering.

The flora system is designed around three layers:

1. **Flora Classification & Properties** — Abstract factories that define a category's base behavior.
2. **Spawning Pipeline** — Deterministic, worker-safe placement rules tied to biomes and surface conditions.
3. **Growth & Player Interaction** — Tick-based growth for crops and saplings, harvesting, and replanting.

### Current Codebase Context

The engine already supports:

- **Foliage flag** (`BlockMaterial.foliage`) — used by leaves to enable alpha-test transparency in the mesher.
- **Two-pass transparency rendering** — opaque pass renders solid blocks; transparent pass renders foliage, glass, and water with alpha blending.
- **Structure stamping** — trees can be implemented as `StructureBlueprint`s stamped during worldgen.
- **Biome-specific surface rules** — `BiomeSurfaceRule` defines top/filler blocks per biome, extensible to flora rules.
- **Inventory & crafting** — harvested flora items flow through the existing inventory system.

---

## 2. Flora Classification & Abstract Factories

Each flora *class* (trees, flowers, crops, etc.) is defined by an abstract factory interface that captures the base properties shared by all members of that class. Individual species (oak tree, rose, wheat) are concrete registrations within that factory.

### 2.1 Base Flora Properties

```typescript
// /src/content/flora/FloraTypes.ts

/** How this flora is rendered in the mesh. */
export const enum FloraRenderType {
  /** Full cube with foliage alpha-test (e.g., leaves). */
  FOLIAGE_CUBE,
  /** Two intersecting quads forming an X (e.g., flowers, saplings). */
  CROSS_QUAD,
  /** Four tall cross-quads for 2-block-high plants (e.g., tall grass, rose bush). */
  TALL_CROSS_QUAD,
  /** Single quad on the side of a block, texture atlas style (e.g., vines). */
  SIDE_ATTACHMENT,
  /** Full cube with custom AABB and no special rendering (e.g., cactus, sugar cane). */
  SOLID_CUSTOM_AABB,
}

/** Soil types that a flora can grow on. */
export const enum SoilType {
  DIRT,
  GRASS,
  SAND,
  FARMLAND,
  /** Any solid block (e.g., vines on stone). */
  ANY_SOLID,
}

/** Light level requirements for growth and survival. */
export interface LightRequirements {
  /** Minimum sky light for survival (0-15). 0 means no minimum. */
  readonly minSkyLight: number;
  /** Minimum block light for survival (0-15). 0 means no minimum. */
  readonly minBlockLight: number;
  /** Maximum sky light for survival (used by nether plants that burn in sunlight). 15 = no max. */
  readonly maxSkyLight: number;
}

/** Growth stages for a flora that matures over time. */
export const enum GrowthStage {
  /** Just planted / initial state. */
  STAGE_0,
  STAGE_1,
  STAGE_2,
  STAGE_3,
  /** Fully grown, ready for harvest. */
  MATURE,
}

/** Base properties shared by all flora. */
export interface FloraProperties {
  /** Numeric block ID in the BlockRegistry. */
  readonly blockId: number;
  /** Human-readable name. */
  readonly name: string;
  /** Render strategy. */
  readonly renderType: FloraRenderType;
  /** Texture layer index for each growth stage. */
  readonly textureLayers: readonly number[];
  /** Soil types this flora can grow on. */
  readonly acceptableSoil: readonly SoilType[];
  /** Light requirements. */
  readonly lightRequirements: LightRequirements;
  /** Custom collision AABB (empty for cross-quad plants). */
  readonly collision: { minX: number; minY: number; minZ: number; maxX: number; maxY: number; maxZ: number };
  /** Whether this flora drops itself when broken (vs dropping seeds). */
  readonly dropsSelf: boolean;
  /** Item ID of the dropped item. */
  readonly dropItemId: number;
  /** Number of items dropped (or random range [min, max]). */
  readonly dropCount: [number, number];
  /** Whether bone meal can be applied to accelerate growth. */
  readonly boneMealable: boolean;
}
```

### 2.2 Abstract Factory Interface

```typescript
// /src/content/flora/FloraFactory.ts

import type { FloraProperties } from "./FloraTypes.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";

/**
 * Abstract factory for a class of flora.
 * Each concrete implementation (e.g., CropFactory, FlowerFactory, TreeFactory)
 * registers its blocks and exposes its property table for use by worldgen and
 * gameplay systems.
 */
export interface FloraCategoryFactory {
  /** Register all blocks in this category with the BlockRegistry. */
  registerBlocks(registry: BlockRegistry): void;

  /** Return the FloraProperties for a given species ID within this category. */
  getProperties(speciesId: number): FloraProperties;

  /** Return all species definitions in this category. */
  getAllProperties(): readonly FloraProperties[];

  /** The category name (e.g., "trees", "flowers", "crops"). */
  readonly categoryName: string;
}
```

### 2.3 Concrete Flora Categories

#### 2.3.1 TreeFactory (Structure-Based Trees)

Trees are spawned as multi-block structures via the existing `StructureBlueprint` system. The `TreeFactory` defines the trunk/leaves block IDs, growth patterns, and per-species blueprints.

```typescript
// /src/content/flora/TreeFactory.ts

import type { FloraCategoryFactory, FloraProperties } from "./FloraTypes.js";
import { FloraRenderType, SoilType, GrowthStage } from "./FloraTypes.js";
import { BlockRegistry } from "../../world/BlockRegistry.js";
import { StructureFactory } from "../structures/StructureRegistry.js";

export interface TreeSpeciesDefinition {
  readonly speciesId: number;
  readonly name: string;
  readonly trunkBlockId: number;
  readonly leavesBlockId: number;
  readonly saplingBlockId: number;
  /** Minimum height of the trunk. */
  readonly minHeight: number;
  /** Extra random height added on top of minHeight. */
  readonly extraHeight: number;
  /** Leaf radius from trunk center. */
  readonly leafRadius: number;
  /** Optional: custom structure blueprint for complex shaped trees. */
  readonly blueprintId?: number;
  /** Soil types the sapling can grow on. */
  readonly acceptableSoil: readonly SoilType[];
  /** Biome IDs where this tree naturally spawns. */
  readonly biomeAffinity: readonly string[];
  /** Spawn weight relative to other trees in the same biome. */
  readonly weight: number;
}

export class TreeFactory implements FloraCategoryFactory {
  readonly categoryName = "trees";

  private readonly species: Map<number, TreeSpeciesDefinition>;

  constructor(
    private readonly speciesList: readonly TreeSpeciesDefinition[],
    private readonly structureFactory?: StructureFactory,
  ) {
    this.species = new Map();
    for (const def of speciesList) {
      this.species.set(def.speciesId, def);
    }
  }

  registerBlocks(registry: BlockRegistry): void {
    // Trees themselves don't register new blocks — trunk and leaves blocks
    // are already registered in VanillaBlockFactory. Saplings are registered
    // as cross-quad flora (see SaplingFactory or inline here).
    // This method exists to satisfy the interface; actual block registration
    // happens in VanillaBlockFactory or a dedicated flora block registration pass.
  }

  getProperties(speciesId: number): FloraProperties {
    const def = this.species.get(speciesId);
    if (!def) throw new Error(`Unknown tree species ${speciesId}`);
    // Return sapling properties — the tree itself is not a single block.
    return {
      blockId: def.saplingBlockId,
      name: `${def.name} Sapling`,
      renderType: FloraRenderType.CROSS_QUAD,
      textureLayers: [def.saplingBlockId], // maps to sapling texture layer
      acceptableSoil: def.acceptableSoil,
      lightRequirements: { minSkyLight: 8, minBlockLight: 0, maxSkyLight: 15 },
      collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
      dropsSelf: true,
      dropItemId: def.saplingBlockId,
      dropCount: [1, 1],
      boneMealable: true,
    };
  }

  getAllProperties(): FloraProperties[] {
    return Array.from(this.species.values()).map((def) => this.getProperties(def.speciesId));
  }

  getSpecies(speciesId: number): TreeSpeciesDefinition {
    const def = this.species.get(speciesId);
    if (!def) throw new Error(`Unknown tree species ${speciesId}`);
    return def;
  }

  getAllSpecies(): readonly TreeSpeciesDefinition[] {
    return this.speciesList;
  }

  /** Build a tree blueprint procedurally based on species parameters. */
  generateBlueprint(def: TreeSpeciesDefinition): StructureBlueprint {
    // See §4.2 for implementation.
    return proceduralTree(def);
  }
}
```

#### 2.3.2 CropFactory (Wheat, Potatoes, Carrots)

Crops are single-block plants with multiple growth stages rendered via cross-quads. Each stage uses a different texture layer.

```typescript
// /src/content/flora/CropFactory.ts

import type { FloraCategoryFactory, FloraProperties } from "./FloraTypes.js";
import { FloraRenderType, SoilType, GrowthStage } from "./FloraTypes.js";

export interface CropSpeciesDefinition {
  readonly speciesId: number;
  readonly name: string;
  readonly blockId: number;
  readonly seedItemId: number;
  readonly harvestItemId: number;
  readonly seedCount: [number, number]; // seeds dropped on harvest
  readonly cropCount: [number, number]; // crop items dropped on harvest
  /** Texture layers for each growth stage (0-3 immature, 4 mature). */
  readonly stageTextures: readonly [number, number, number, number, number];
  /** Total growth time in game ticks at default tick rate. */
  readonly growthTicks: number;
  /** Soil types this crop can be planted on (typically farmland only). */
  readonly acceptableSoil: readonly SoilType[];
}

const CROP_STAGES: readonly GrowthStage[] = [
  GrowthStage.STAGE_0,
  GrowthStage.STAGE_1,
  GrowthStage.STAGE_2,
  GrowthStage.STAGE_3,
  GrowthStage.MATURE,
];

export class CropFactory implements FloraCategoryFactory {
  readonly categoryName = "crops";

  private readonly crops: Map<number, CropSpeciesDefinition>;

  constructor(private readonly cropList: readonly CropSpeciesDefinition[]) {
    this.crops = new Map();
    for (const def of cropList) {
      this.crops.set(def.speciesId, def);
    }
  }

  registerBlocks(registry: BlockRegistry): void {
    for (const def of this.cropList) {
      // Each growth stage is registered as a separate block variant or
      // we use a single block ID with metadata-driven texture selection.
      // The current engine uses block IDs directly; for crops we register
      // one block per species and encode the growth stage in the block's
      // additional data (or use adjacent block IDs per stage).
      //
      // For simplicity in the current SharedPool architecture, we register
      // 5 block IDs per crop (one per stage). Stage 0-3 are immature,
      // stage 4 is mature.
      for (let stage = 0; stage < 5; stage++) {
        const stageBlockId = def.blockId + stage;
        if (registry.tryGet(stageBlockId)) continue;
        registry.register({
          id: stageBlockId,
          name: `${def.name} (Stage ${stage})`,
          textures: {
            top: def.stageTextures[stage],
            bottom: def.stageTextures[stage],
            side: def.stageTextures[stage],
          },
          material: {
            opaque: false,
            transparent: true,
            liquid: false,
            foliage: true,
            lightEmission: 0,
          },
          collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
        });
      }
    }
  }

  getProperties(speciesId: number): FloraProperties {
    const def = this.crops.get(speciesId);
    if (!def) throw new Error(`Unknown crop species ${speciesId}`);
    return {
      blockId: def.blockId,
      name: def.name,
      renderType: FloraRenderType.CROSS_QUAD,
      textureLayers: def.stageTextures,
      acceptableSoil: def.acceptableSoil,
      lightRequirements: { minSkyLight: 8, minBlockLight: 0, maxSkyLight: 15 },
      collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
      dropsSelf: false,
      dropItemId: def.harvestItemId,
      dropCount: def.cropCount,
      boneMealable: true,
    };
  }

  getAllProperties(): FloraProperties[] {
    return this.cropList.map((def) => this.getProperties(def.speciesId));
  }

  getGrowthStageBlockId(speciesId: number, stage: GrowthStage): number {
    const def = this.crops.get(speciesId);
    if (!def) throw new Error(`Unknown crop species ${speciesId}`);
    return def.blockId + stage;
  }
}
```

#### 2.3.3 FlowerFactory (Small Plants)

Flowers and small plants are single-block cross-quad flora with no growth stages. They spawn naturally on grass/dirt in specific biomes.

```typescript
// /src/content/flora/FlowerFactory.ts

import type { FloraCategoryFactory, FloraProperties } from "./FloraTypes.js";
import { FloraRenderType, SoilType } from "./FloraTypes.js";

export interface FlowerSpeciesDefinition {
  readonly speciesId: number;
  readonly name: string;
  readonly blockId: number;
  readonly textureLayer: number;
  readonly acceptableSoil: readonly SoilType[];
  readonly biomeAffinity: readonly string[];
  readonly spawnChance: number; // 0.0 - 1.0 probability per eligible surface block
  /** Whether this is a 2-block-tall plant (e.g., rose bush, peony). */
  readonly isTall: boolean;
}

export class FlowerFactory implements FloraCategoryFactory {
  readonly categoryName = "flowers";

  private readonly flowers: Map<number, FlowerSpeciesDefinition>;

  constructor(private readonly flowerList: readonly FlowerSpeciesDefinition[]) {
    this.flowers = new Map();
    for (const def of flowerList) {
      this.flowers.set(def.speciesId, def);
    }
  }

  registerBlocks(registry: BlockRegistry): void {
    for (const def of this.flowerList) {
      registry.register({
        id: def.blockId,
        name: def.name,
        textures: {
          top: def.textureLayer,
          bottom: def.textureLayer,
          side: def.textureLayer,
        },
        material: {
          opaque: false,
          transparent: true,
          liquid: false,
          foliage: true,
          lightEmission: 0,
        },
        collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
      });
    }
  }

  getProperties(speciesId: number): FloraProperties {
    const def = this.flowers.get(speciesId);
    if (!def) throw new Error(`Unknown flower species ${speciesId}`);
    return {
      blockId: def.blockId,
      name: def.name,
      renderType: def.isTall ? FloraRenderType.TALL_CROSS_QUAD : FloraRenderType.CROSS_QUAD,
      textureLayers: [def.textureLayer],
      acceptableSoil: def.acceptableSoil,
      lightRequirements: { minSkyLight: 8, minBlockLight: 0, maxSkyLight: 15 },
      collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
      dropsSelf: true,
      dropItemId: def.blockId,
      dropCount: [1, 1],
      boneMealable: false,
    };
  }

  getAllProperties(): FloraProperties[] {
    return this.flowerList.map((def) => this.getProperties(def.speciesId));
  }
}
```

#### 2.3.4 GrassFactory (Tall Grass & Ferns)

Grass blocks are similar to flowers but can also drop seeds and are more abundant.

```typescript
// /src/content/flora/GrassFactory.ts

import { FloraRenderType, SoilType } from "./FloraTypes.js";
import type { FloraCategoryFactory, FloraProperties } from "./FloraTypes.js";

export interface GrassSpeciesDefinition {
  readonly speciesId: number;
  readonly name: string;
  readonly blockId: number;
  readonly textureLayer: number;
  readonly acceptableSoil: readonly SoilType[];
  readonly biomeAffinity: readonly string[];
  readonly spawnChance: number;
  readonly isTall: boolean;
  /** Whether this grass can be harvested for seeds. */
  readonly dropsSeeds: boolean;
  readonly seedItemId: number;
}

export class GrassFactory implements FloraCategoryFactory {
  readonly categoryName = "grass";

  private readonly grasses: Map<number, GrassSpeciesDefinition>;

  constructor(private readonly grassList: readonly GrassSpeciesDefinition[]) {
    this.grasses = new Map();
    for (const def of grassList) {
      this.grasses.set(def.speciesId, def);
    }
  }

  registerBlocks(registry: BlockRegistry): void {
    for (const def of this.grassList) {
      registry.register({
        id: def.blockId,
        name: def.name,
        textures: {
          top: def.textureLayer,
          bottom: def.textureLayer,
          side: def.textureLayer,
        },
        material: {
          opaque: false,
          transparent: true,
          liquid: false,
          foliage: true,
          lightEmission: 0,
        },
        collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
      });
    }
  }

  getProperties(speciesId: number): FloraProperties {
    const def = this.grasses.get(speciesId);
    if (!def) throw new Error(`Unknown grass species ${speciesId}`);
    return {
      blockId: def.blockId,
      name: def.name,
      renderType: def.isTall ? FloraRenderType.TALL_CROSS_QUAD : FloraRenderType.CROSS_QUAD,
      textureLayers: [def.textureLayer],
      acceptableSoil: def.acceptableSoil,
      lightRequirements: { minSkyLight: 8, minBlockLight: 0, maxSkyLight: 15 },
      collision: { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 },
      dropsSelf: true,
      dropItemId: def.blockId,
      dropCount: [1, 1],
      boneMealable: false,
    };
  }

  getAllProperties(): FloraProperties[] {
    return this.grassList.map((def) => this.getProperties(def.speciesId));
  }
}
```

#### 2.3.5 Other Flora Categories

The same pattern extends to:

- **VineFactory** — `SIDE_ATTACHMENT` render type, grows down from solid blocks.
- **CactusFactory** — `SOLID_CUSTOM_AABB`, grows on sand, damages on contact.
- **SugarCaneFactory** — `SOLID_CUSTOM_AABB`, grows on sand/dirt near water, grows up to 3 blocks tall.
- **MushroomFactory** — Similar to flowers but with `minSkyLight: 0, maxSkyLight: 12` (nether fungi thrive in darkness).
- **WaterLilyFactory** — `CROSS_QUAD` placed on water surface.

---

## 3. Flora Registry & Composition Root

The `FloraRegistry` aggregates all category factories and provides a unified lookup for worldgen and gameplay systems.

```typescript
// /src/content/flora/FloraRegistry.ts

import type { FloraCategoryFactory, FloraProperties } from "./FloraTypes.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";

export class FloraRegistry {
  private readonly categories = new Map<string, FloraCategoryFactory>();
  private readonly byBlockId = new Map<number, FloraProperties>();

  registerCategory(factory: FloraCategoryFactory): void {
    this.categories.set(factory.categoryName, factory);
    for (const props of factory.getAllProperties()) {
      this.byBlockId.set(props.blockId, props);
    }
  }

  getCategory(name: string): FloraCategoryFactory | undefined {
    return this.categories.get(name);
  }

  getProperties(blockId: number): FloraProperties | undefined {
    return this.byBlockId.get(blockId);
  }

  isFlora(blockId: number): boolean {
    return this.byBlockId.has(blockId);
  }

  registerAllBlocks(registry: BlockRegistry): void {
    for (const factory of this.categories.values()) {
      factory.registerBlocks(registry);
    }
  }

  getAllCategories(): readonly FloraCategoryFactory[] {
    return Array.from(this.categories.values());
  }
}
```

### Default Flora Configuration

```typescript
// /src/content/flora/DefaultFlora.ts

import { FloraRegistry } from "./FloraRegistry.js";
import { TreeFactory } from "./TreeFactory.js";
import { CropFactory } from "./CropFactory.js";
import { FlowerFactory } from "./FlowerFactory.js";
import { GrassFactory } from "./GrassFactory.js";
import { SoilType } from "./FloraTypes.js";

export const createDefaultFloraRegistry = (): FloraRegistry => {
  const registry = new FloraRegistry();

  // --- Trees ---
  registry.registerCategory(new TreeFactory([
    {
      speciesId: 0,
      name: "Oak",
      trunkBlockId: 17,   // log
      leavesBlockId: 18,  // leaves
      saplingBlockId: 100, // oak sapling
      minHeight: 4,
      extraHeight: 3,
      leafRadius: 2,
      acceptableSoil: [SoilType.DIRT, SoilType.GRASS],
      biomeAffinity: ["plains", "forest", "swamp"],
      weight: 10,
    },
    {
      speciesId: 1,
      name: "Birch",
      trunkBlockId: 102,  // birch log
      leavesBlockId: 103, // birch leaves
      saplingBlockId: 101,
      minHeight: 5,
      extraHeight: 2,
      leafRadius: 2,
      acceptableSoil: [SoilType.DIRT, SoilType.GRASS],
      biomeAffinity: ["forest"],
      weight: 5,
    },
    {
      speciesId: 2,
      name: "Pine",
      trunkBlockId: 104,  // spruce log
      leavesBlockId: 105, // spruce leaves
      saplingBlockId: 106,
      minHeight: 6,
      extraHeight: 4,
      leafRadius: 2,
      acceptableSoil: [SoilType.DIRT, SoilType.GRASS],
      biomeAffinity: ["mountains", "forest"],
      weight: 7,
      // Pine uses a tapered leaf pattern, handled by proceduralTree()
    },
  ]));

  // --- Crops ---
  registry.registerCategory(new CropFactory([
    {
      speciesId: 0,
      name: "Wheat",
      blockId: 110, // base block ID, stages use 110-114
      seedItemId: 115, // wheat seeds
      harvestItemId: 116, // wheat
      seedCount: [1, 3],
      cropCount: [1, 1],
      stageTextures: [200, 201, 202, 203, 204], // texture layers for each stage
      growthTicks: 2400, // ~2 minutes at 20 ticks/sec
      acceptableSoil: [SoilType.FARMLAND],
    },
    {
      speciesId: 1,
      name: "Potato",
      blockId: 120,
      seedItemId: 121, // potato
      harvestItemId: 121, // potato
      seedCount: [1, 1],
      cropCount: [1, 4],
      stageTextures: [205, 206, 207, 208, 209],
      growthTicks: 2400,
      acceptableSoil: [SoilType.FARMLAND],
    },
    {
      speciesId: 2,
      name: "Carrot",
      blockId: 130,
      seedItemId: 131, // carrot
      harvestItemId: 131, // carrot
      seedCount: [1, 1],
      cropCount: [1, 4],
      stageTextures: [210, 211, 212, 213, 214],
      growthTicks: 2400,
      acceptableSoil: [SoilType.FARMLAND],
    },
  ]));

  // --- Flowers ---
  registry.registerCategory(new FlowerFactory([
    {
      speciesId: 0, name: "Dandelion", blockId: 140, textureLayer: 220,
      acceptableSoil: [SoilType.GRASS], biomeAffinity: ["plains", "forest"],
      spawnChance: 0.02, isTall: false,
    },
    {
      speciesId: 1, name: "Rose", blockId: 141, textureLayer: 221,
      acceptableSoil: [SoilType.GRASS], biomeAffinity: ["forest"],
      spawnChance: 0.01, isTall: false,
    },
    {
      speciesId: 2, name: "Rose Bush", blockId: 142, textureLayer: 222,
      acceptableSoil: [SoilType.GRASS], biomeAffinity: ["forest"],
      spawnChance: 0.005, isTall: true,
    },
  ]));

  // --- Grass ---
  registry.registerCategory(new GrassFactory([
    {
      speciesId: 0, name: "Tall Grass", blockId: 150, textureLayer: 230,
      acceptableSoil: [SoilType.GRASS], biomeAffinity: ["plains", "forest"],
      spawnChance: 0.1, isTall: false, dropsSeeds: true, seedItemId: 115,
    },
    {
      speciesId: 1, name: "Fern", blockId: 151, textureLayer: 231,
      acceptableSoil: [SoilType.GRASS], biomeAffinity: ["forest", "swamp"],
      spawnChance: 0.05, isTall: false, dropsSeeds: false, seedItemId: 0,
    },
    {
      speciesId: 2, name: "Double Tallgrass", blockId: 152, textureLayer: 232,
      acceptableSoil: [SoilType.GRASS], biomeAffinity: ["plains"],
      spawnChance: 0.03, isTall: true, dropsSeeds: true, seedItemId: 115,
    },
  ]));

  return registry;
};
```

---

## 4. Flora Spawning Pipeline

Flora spawning happens in two phases:

1. **World Generation** — Deterministic placement during chunk generation in `WorldGenWorker`.
2. **Growth & Player Planting** — Tick-driven growth and manual placement during gameplay.

### 4.1 WorldGen Integration

The `WorldGenPipeline` is extended with a `FloraDecorator` that runs after terrain, caves, ores, and structures are placed. It operates within the same worker, using the same deterministic PRNG seed for reproducibility.

```typescript
// /src/engine/workers/worldgen/FloraDecorator.ts

import type { FloraRegistry } from "../../../content/flora/FloraRegistry.js";
import type { BiomeSurfaceRule } from "../../../content/biomes/BiomeSurfaceRule.js";

/**
 * Decorates terrain with flora after the base terrain pass.
 * Runs inside WorldGenWorker, operates on the same SharedPool slot.
 * O(V) scan of surface blocks, fully deterministic.
 */
export class FloraDecorator {
  constructor(
    private readonly flora: FloraRegistry,
  ) {}

  decorate(
    voxels: Uint8Array,
    light: Uint8Array,
    dims: { sizeX: number; sizeY: number; sizeZ: number },
    chunkX: number,
    chunkZ: number,
    biome: BiomeSurfaceRule,
    rng: () => number, // deterministic PRNG seeded by chunk position
  ): void {
    const { sizeX, sizeY, sizeZ } = dims;
    const baseX = chunkX * sizeX;
    const baseZ = chunkZ * sizeZ;

    // Collect all surface blocks (blocks with air above)
    for (let z = 0; z < sizeZ; z++) {
      for (let x = 0; x < sizeX; x++) {
        const worldX = baseX + x;
        const worldZ = baseZ + z;

        for (let y = sizeY - 1; y >= 0; y--) {
          const index = (y * sizeZ + z) * sizeX + x;
          const blockId = voxels[index];

          if (blockId === 0) continue;

          // The block above must be air for surface flora to spawn
          const aboveIndex = ((y + 1) * sizeZ + z) * sizeX + x;
          if (y + 1 >= sizeY || voxels[aboveIndex] !== 0) break;

          // Sample biome-specific flora rules
          this.tryPlaceFlora(voxels, dims, x, y, z, blockId, biome, worldX, worldZ, rng);
          break; // Only the topmost block in this column
        }
      }
    }
  }

  private tryPlaceFlora(
    voxels: Uint8Array,
    dims: { sizeX: number; sizeY: number; sizeZ: number },
    x: number, y: number, z: number,
    surfaceBlockId: number,
    biome: BiomeSurfaceRule,
    worldX: number, worldZ: number,
    rng: () => number,
  ): void {
    // This is where biome-specific flora placement rules are evaluated.
    // The concrete logic is delegated to a BiomeFloraRules table that maps
    // biome name -> list of (FloraProperties, spawnChance) entries.
    //
    // For each eligible flora in the biome:
    //   1. Check if rng() < spawnChance
    //   2. Check soil compatibility (surfaceBlockId matches acceptableSoil)
    //   3. Check light requirements (read from light array)
    //   4. If tall plant, check that 2 blocks of air exist above
    //   5. Place the block
    //
    // Trees are delegated to a TreePlanner that evaluates canopy space
    // and stamps a tree blueprint if enough room exists.
  }
}
```

### 4.2 Procedural Tree Generation

Trees are generated procedurally from species parameters rather than being hand-crafted blueprints. This keeps memory usage low and allows infinite variety.

```typescript
// /src/content/flora/TreeGenerator.ts

import { stampBlueprint, type StructureBlueprint } from "../structures/StructureBlueprint.js";

/**
 * Generates a tree StructureBlueprint from species parameters.
 * Produces a compact packed-block representation suitable for
 * the existing stampBlueprint pipeline.
 *
 * O(volume) where volume ≈ trunkHeight * (leafRadius * 2)^2.
 */
export function proceduralTree(def: {
  trunkBlockId: number;
  leavesBlockId: number;
  minHeight: number;
  extraHeight: number;
  leafRadius: number;
  rng: () => number;
}): StructureBlueprint {
  const height = def.minHeight + Math.floor(def.extraHeight * def.rng());
  const radius = def.leafRadius;
  const sizeX = radius * 2 + 3;
  const sizeY = height + radius + 2;
  const sizeZ = radius * 2 + 3;
  const blocks: number[] = [];

  // Trunk
  const trunkX = Math.floor(sizeX / 2);
  const trunkZ = Math.floor(sizeZ / 2);
  for (let y = 0; y < height; y++) {
    blocks.push(trunkX, y, trunkZ, def.trunkBlockId);
  }

  // Leaves — spherical canopy
  const leafBaseY = height - 1 - Math.floor(radius / 2);
  for (let dy = 0; dy <= radius + 1; dy++) {
    for (let dx = -radius; dx <= radius; dx++) {
      for (let dz = -radius; dz <= radius; dz++) {
        const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > radius + 0.5) continue;
        // Don't replace the trunk top
        const wx = trunkX + dx;
        const wz = trunkZ + dz;
        if (wx === trunkX && wz === trunkZ && dy + leafBaseY >= height - 1) continue;
        blocks.push(wx, leafBaseY + dy, wz, def.leavesBlockId);
      }
    }
  }

  return {
    id: -1, // procedural, no fixed ID
    sizeX,
    sizeY,
    sizeZ,
    blocks: packBlocks(blocks),
    paletteWeight: 1,
  };
}

function packBlocks(blocks: number[]): Uint8Array {
  const packed = new Uint8Array(blocks.length + 4);
  for (let i = 0; i < blocks.length; i++) {
    packed[i] = blocks[i] & 0xff;
  }
  // Terminator
  packed[blocks.length] = 0x80;
  packed[blocks.length + 1] = 0;
  packed[blocks.length + 2] = 0;
  packed[blocks.length + 3] = 0;
  return packed;
}
```

### 4.3 Biome-Specific Flora Rules

```typescript
// /src/content/flora/BiomeFloraRules.ts

import type { FloraRegistry } from "./FloraRegistry.js";
import type { TreeSpeciesDefinition } from "./TreeFactory.js";
import type { FlowerSpeciesDefinition } from "./FlowerFactory.js";
import type { GrassSpeciesDefinition } from "./GrassFactory.js";

export interface BiomeFloraEntry {
  /** The block ID of the flora to place. */
  readonly blockId: number;
  /** Probability 0.0-1.0 that this flora spawns on an eligible surface block. */
  readonly spawnChance: number;
  /** Whether this flora requires 2 blocks of vertical space (tall plants). */
  readonly requiresTwoBlocks: boolean;
  /** Optional tree species definition — if set, generates a tree instead of a single block. */
  readonly treeSpecies?: TreeSpeciesDefinition;
}

/**
 * Maps biome names to their flora populations.
 * Each biome entry is a list of (flora, weight) pairs.
 */
export const BIOME_FLORA_RULES: Record<string, readonly BiomeFloraEntry[]> = {
  plains: [
    { blockId: 150, spawnChance: 0.10, requiresTwoBlocks: false }, // tall grass
    { blockId: 152, spawnChance: 0.03, requiresTwoBlocks: true },  // double tallgrass
    { blockId: 140, spawnChance: 0.02, requiresTwoBlocks: false }, // dandelion
  ],
  forest: [
    { blockId: 150, spawnChance: 0.08, requiresTwoBlocks: false }, // tall grass
    { blockId: 151, spawnChance: 0.04, requiresTwoBlocks: false }, // fern
    { blockId: 140, spawnChance: 0.01, requiresTwoBlocks: false }, // dandelion
    { blockId: 141, spawnChance: 0.01, requiresTwoBlocks: false }, // rose
    { blockId: 142, spawnChance: 0.005, requiresTwoBlocks: true }, // rose bush
  ],
  desert: [
    // Cactus and dead bushes — defined separately via CactusFactory
  ],
  swamp: [
    { blockId: 151, spawnChance: 0.06, requiresTwoBlocks: false }, // fern
  ],
  mountains: [
    { blockId: 150, spawnChance: 0.03, requiresTwoBlocks: false }, // sparse tall grass
  ],
};

/**
 * Tree spawn rules per biome.
 * Trees are spawned independently from smaller flora, using a separate
 * density pass that evaluates canopy coverage.
 */
export const BIOME_TREE_RULES: Record<string, {
  readonly speciesId: number;
  readonly density: number; // average trees per chunk (can be fractional)
}[]> = {
  plains: [{ speciesId: 0, density: 0.5 }],  // 50% of chunks have an oak
  forest: [
    { speciesId: 0, density: 5.0 },  // oak
    { speciesId: 1, density: 2.0 },  // birch
  ],
  swamp: [{ speciesId: 0, density: 3.0 }],  // oak with vines
  mountains: [{ speciesId: 2, density: 1.5 }], // pine
  desert: [],
};
```

---

## 5. Growth & Farming Systems

### 5.1 Crop Growth System

Crops advance through growth stages on random game ticks. The `CropGrowthSystem` runs on the main thread as an ECS system, processing loaded chunks within the player's simulation radius.

```typescript
// /src/engine/ecs/systems/CropGrowthSystem.ts

import type { World } from "../../../world/World.js";
import type { FloraRegistry } from "../../../content/flora/FloraRegistry.js";
import { GrowthStage } from "../../../content/flora/FloraTypes.js";
import type { CropFactory, CropSpeciesDefinition } from "../../../content/flora/CropFactory.js";

/**
 * Processes crop growth in loaded chunks.
 * Runs every N game ticks (configurable, typically every 20 ticks = 1 second).
 *
 * O(surface_blocks_in_loaded_area) — only iterates exposed crop blocks.
 */
export class CropGrowthSystem {
  private static readonly TICK_INTERVAL = 20; // game ticks between growth attempts

  private tickCounter = 0;

  constructor(
    private readonly world: World,
    private readonly flora: FloraRegistry,
    private readonly randomTickSpeed: number = 3, // blocks per chunk per tick (MC default)
  ) {}

  update(deltaTicks: number): void {
    this.tickCounter += deltaTicks;
    if (this.tickCounter < CropGrowthSystem.TICK_INTERVAL) return;
    this.tickCounter = 0;

    // Iterate all loaded chunks within simulation distance
    for (const [key, chunk] of this.world.entries()) {
      if (chunk.state !== "uploaded") continue;
      this.tickChunkCrops(chunk);
    }
  }

  private tickChunkCrops(chunk: Chunk): void {
    const slot = this.world.getChunkSlot(chunk);
    const { sizeX, sizeY, sizeZ } = slot.dims; // (actual dims omitted for brevity)
    const voxels = slot.voxels;
    const light = slot.light;

    // Random tick: select random blocks in the chunk
    for (let i = 0; i < this.randomTickSpeed * (sizeX * sizeZ) / 256; i++) {
      const x = (Math.random() * sizeX) | 0;
      const z = (Math.random() * sizeZ) | 0;
      const y = (Math.random() * sizeY) | 0;
      const index = (y * sizeZ + z) * sizeX + x;
      const blockId = voxels[index];

      const props = this.flora.getProperties(blockId);
      if (!props) continue;

      // Check if this is a crop with growth stages
      const cropFactory = this.flora.getCategory("crops") as CropFactory | undefined;
      if (!cropFactory) continue;

      // Determine current growth stage and advance if conditions are met
      this.attemptGrowth(voxels, light, index, blockId, props, sizeX, sizeZ);
    }
  }

  private attemptGrowth(
    voxels: Uint8Array,
    light: Uint8Array,
    index: number,
    blockId: number,
    props: FloraProperties,
    sizeX: number,
    sizeZ: number,
  ): void {
    // Extract sky light and block light at this position
    const packedLight = light[index];
    const skyLight = packedLight & 0x0f;
    const blockLight = (packedLight >> 4) & 0x0f;

    // Check light requirements
    if (skyLight < props.lightRequirements.minSkyLight) return;
    if (blockLight < props.lightRequirements.minBlockLight) return;

    // Determine current stage from block ID offset
    // (Stage 4 = mature, no further growth)
    const stageOffset = blockId - props.blockId;
    if (stageOffset >= 4) return; // already mature

    // Advance to next stage with random probability
    if (Math.random() < 0.5) {
      voxels[index] = blockId + 1;
      this.world.markChunkDirty(/* chunk coords from index */);
      this.world.requestRemesh(/* chunk ref */);
    }
  }
}
```

### 5.2 Player Planting & Harvesting

Players interact with flora through the existing `PlayerInteractionController`. The interaction flow is:

1. **Right-click on soil with seeds** → Plants a crop at the target position.
2. **Right-click on mature crop** → Harvests the crop, drops items, resets to Stage 0.
3. **Left-click on any flora** → Breaks it, drops appropriate items.
4. **Right-click on sapling with bone meal** → Accelerates growth (if boneMealable).

```typescript
// Extensions to PlayerInteractionController (conceptual)

/**
 * Handle right-click on a block.
 * Returns true if the interaction was consumed.
 */
function onRightClickBlock(
  worldX: number, worldY: number, worldZ: number,
  heldItemId: number,
): boolean {
  // Check if holding seeds and target is farmland
  if (isSeedItem(heldItemId)) {
    const targetBlock = world.getBlockIdAt(worldX, worldY, worldZ);
    if (isFarmland(targetBlock)) {
      const aboveBlock = world.getBlockIdAt(worldX, worldY + 1, worldZ);
      if (aboveBlock === 0) {
        // Plant the crop
        const cropBlockId = getCropBlockIdForSeed(heldItemId);
        world.setBlockIdAt(worldX, worldY + 1, worldZ, cropBlockId);
        inventory.removeOne(heldItemId);
        return true;
      }
    }
  }

  // Check if target is a mature crop — harvest it
  const targetBlock = world.getBlockIdAt(worldX, worldY, worldZ);
  const floraProps = floraRegistry.getProperties(targetBlock);
  if (floraProps && !floraProps.dropsSelf) {
    const stageOffset = targetBlock - floraProps.blockId;
    if (stageOffset >= 4) {
      // Mature — harvest
      harvestCrop(worldX, worldY, worldZ, floraProps);
      return true;
    }
  }

  return false;
}
```

### 5.3 Farmland Block

Farmland is a new block that tills dirt/grass when right-clicked with a hoe. It must be hydrated by nearby water (within 4 blocks) for crops to grow.

```typescript
// Farmland block definition (added to VanillaBlockFactory)
registry.register({
  id: 60, // MC 1.5.2 farmland ID
  name: "farmland",
  textures: { top: Tex.FARMLAND_DRY, bottom: Tex.DIRT, side: Tex.DIRT },
  material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
  collision: { minX: 0, minY: 0, minZ: 0, maxX: 1, maxY: 0.9375, maxZ: 1 }, // slightly shorter
});
```

---

## 6. Rendering Flora

### 6.1 Cross-Quad Rendering

Flowers, grass, saplings, and crops use cross-quad rendering — two perpendicular quads that form an X shape, always facing the camera. This requires extending the mesher.

```typescript
// Mesher extension for cross-quad flora (conceptual)

/**
 * Detects flora blocks with CROSS_QUAD or TALL_CROSS_QUAD render type
 * and emits two (or four) perpendicular quads instead of full faces.
 *
 * Cross-quad vertices have:
 * - Positions computed from the block center with width/height determined
 *   by the flora properties
 * - UVs mapping the full texture tile
 * - Normals pointing toward the camera (computed in vertex shader)
 * - Light data sampled at the block position
 */
function meshCrossQuad(
  voxels: Uint8Array,
  vertices: Float32Array,
  indices: Uint32Array,
  x: number, y: number, z: number,
  blockId: number,
  textureLayer: number,
  isTall: boolean,
): { vertexCount: number; indexCount: number } {
  const height = isTall ? 2.0 : 1.0;
  const width = 0.8; // slightly less than full block for visual separation
  const offset = (1.0 - width) / 2;

  // Two perpendicular quads
  // Quad 1: along X axis (normal facing Z)
  // Quad 2: along Z axis (normal facing X)
  //
  // Each quad is 4 vertices + 6 indices
  // Tall plants stack two cross-quads vertically

  // ... vertex and index emission logic ...
}
```

### 6.2 Foliage Pass in Fragment Shader

The existing two-pass transparency rendering handles flora correctly:

- **Opaque pass** — Discards fragments with `albedo.a < 0.95`. Foliage textures with alpha < 0.95 are skipped, preserving depth for solid geometry behind them.
- **Transparent pass** — Renders all semi-transparent foliage, leaves, glass, and water with correct alpha blending.

The `foliage` flag in `BlockMaterial` is used by the mesher's `FaceCuller` to decide whether a leaf block occludes its neighbor:

```typescript
// In FaceCuller.ts — extended to handle foliage
export const shouldCullFace = (a: number, b: number, blocks: BlockRegistry): boolean => {
  if (a === b) return true;
  if (a === 0 || b === 0) return false;

  const blockA = blocks.get(a);
  const blockB = blocks.get(b);

  // Foliage blocks don't cull faces against other foliage
  // (leaves should render inside the canopy)
  if (blockA.material.foliage && blockB.material.foliage) return false;

  return blockA.material.opaque && blockB.material.opaque;
};
```

### 6.3 Texture Layers for Flora

New texture layers must be added to:

1. `Tex` enum in `src/world/blocks/TextureLayers.ts`
2. `LAYER_COLORS` in `src/engine/assets/TexturePipeline.ts`

Example additions for flora textures:

```typescript
// In TextureLayers.ts
export const enum Tex {
  // ... existing layers ...
  // Flora textures (reserve a block of IDs)
  OAK_SAPLING = 100,
  BIRCH_SAPLING = 101,
  BIRCH_LOG = 102,
  BIRCH_LEAVES = 103,
  SPRUCE_LOG = 104,
  SPRUCE_LEAVES = 105,
  SPRUCE_SAPLING = 106,
  // Crops
  WHEAT_STAGE_0 = 110,
  WHEAT_STAGE_1 = 111,
  WHEAT_STAGE_2 = 112,
  WHEAT_STAGE_3 = 113,
  WHEAT_MATURE = 114,
  // ... etc
}
```

---

## 7. Integration Into the Existing Pipeline

### 7.1 Chunk Generation Flow (Updated)

```
WorldGenWorker.generate()
├── 1. Terrain heightmap & 3D noise density
├── 2. Biome surface rules (top/filler blocks)
├── 3. Cave carving
├── 4. Structure stamping (villages, etc.)
├── 5. Ore distribution
├── 6. [NEW] Flora decoration
│   ├── 6a. Surface flora (grass, flowers) — per-biome rules
│   ├── 6b. Tree placement — canopy check + procedural tree stamp
│   └── 6c. Cactus/sugar cane — special placement rules
└── 7. Mark slot as VOXELS_READY
```

### 7.2 Block Registration Flow (Updated)

```
Game constructor
├── new BlockRegistry()
├── new VanillaBlockFactory().registerAll(blockRegistry)
├── [NEW] new FloraRegistry()
├── [NEW] createDefaultFloraRegistry(blockRegistry)
├── new Renderer(gl, blockRegistry, config)
└── new World(pool, genWorkers, meshWorkers, blockRegistry, config)
```

### 7.3 WorldGen Worker Initialization (Updated)

```typescript
// WorldGenPipeline constructor
constructor(seed: number, biomeRegistry = new BiomeRegistry(), floraRegistry?: FloraRegistry) {
  this.densityNoise = new SimplexNoise(seed);
  this.biomeSampler = new BiomeSampler(seed, biomeRegistry);
  this.caveCarver = new CaveCarver(seed);
  this.oreDist = new OreDistributor(seed);
  this.structureFactory = new StructureFactory();
  this.villagePlanner = new VillagePlanner(this.structureFactory.getAll());
  // NEW
  this.floraDecorator = new FloraDecorator(floraRegistry ?? new FloraRegistry());
}
```

### 7.4 Data Flow for Flora State

Flora growth stages are stored directly in the voxel data (different block IDs for different stages). This is consistent with the existing architecture where block ID encodes all state. No additional component storage is needed for crop stage tracking.

| Flora Type | State Encoding | Storage |
|---|---|---|
| Crop (wheat, etc.) | 5 block IDs per crop (stage 0-4) | Voxel Uint8Array |
| Flowers/Grass | 1 block ID per species | Voxel Uint8Array |
| Saplings | 1 block ID per species | Voxel Uint8Array |
| Trees | Multi-block structure | Voxel Uint8Array |
| Vines | Block ID + metadata in redstone array (optional) | Voxel + Redstone arrays |

---

## 8. Summary of New Files

```
src/content/flora/
├── FloraTypes.ts          — Base interfaces, enums (FloraRenderType, SoilType, GrowthStage, FloraProperties)
├── FloraFactory.ts        — FloraCategoryFactory interface
├── FloraRegistry.ts       — Aggregates all category factories
├── DefaultFlora.ts        — Default flora configuration (trees, crops, flowers, grass)
├── TreeFactory.ts         — Tree species definitions & factory
├── TreeGenerator.ts       — Procedural tree blueprint generation
├── CropFactory.ts         — Crop species definitions & factory
├── FlowerFactory.ts       — Flower species definitions & factory
├── GrassFactory.ts        — Grass species definitions & factory
├── BiomeFloraRules.ts     — Per-biome flora spawn tables

src/engine/workers/worldgen/
├── FloraDecorator.ts      — WorldGen worker flora placement pass

src/engine/ecs/systems/
├── CropGrowthSystem.ts    — Tick-based crop growth system
```

---

## 9. Future Extensions

- **Vines** — `SIDE_ATTACHMENT` render type, grows downward, requires solid block adjacency.
- **Sugar Cane** — Grows up to 3 blocks tall on sand/dirt near water.
- **Cactus** — Existing block with custom AABB; extend with growth mechanics.
- **Mushrooms** — Light-sensitive spawn rules (dark areas, nether).
- **Bone Meal** — Accelerate growth for applicable flora.
- **Pumpkins & Melons** — Grown from seeds, spread to adjacent blocks.
- **Nether Flora** — Nether wart, fungi — different light requirements.
- **Color/Shade Variation** — Biome-tinted grass and foliage (e.g., swamp grass is darker).
- **Cross-Quad Meshing** — Full mesher support for `CROSS_QUAD` and `TALL_CROSS_QUAD` render types.
