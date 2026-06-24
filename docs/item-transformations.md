# Item Transformation System — Crafting, Processing, and Mutation

**Version:** 1.0  
**Scope:** Every way an item can change into another item or class of item — crafting, smelting, alchemy, enchanting, repairing, salvaging, dyeing, cooking, infusing, and dynamic transformations.  
**Architecture Constraints:** Strict TypeScript, Data-Oriented Design, ECS integration, deterministic recipes, zero-copy inventory access.

---

## 1. System Overview

Items are not static. A piece of raw ore becomes an ingot in a forge. A plant and a vial become a potion at an alchemy station. A damaged sword is repaired with an ingot at a grindstone. An ordinary blade becomes a flaming blade when infused with a fire gem.

The transformation system is a generalization of the existing crafting system: any item can be transformed into any other item (or class of item) through a defined *process* at a designated *station* with optional *tools* and *skill checks*.

### Transformation Categories

| Category | Station | Input | Output | Preserves Metadata? |
|:---------|:--------|:------|:-------|:--------------------|
| **Crafting** | Workbench (or player grid) | Materials | Tools, blocks, items | No |
| **Smelting** | Forge / Furnace | Ore, food, clay | Ingots, cooked food, bricks | No |
| **Alchemy** | Alchemy Table | Plants, vials, essences | Potions, poisons, oils | No |
| **Infusion** | Infusion Altar | Item + gem/essence | Enchanted/imbued item | Yes — preserves durability, adds modifiers |
| **Repair** | Anvil / Grindstone | Damaged item + material | Repaired item | Yes — increases durability |
| **Salvage** | Disassembly Table | Tool, weapon, armor | Raw materials | No |
| **Cooking** | Campfire / Kitchen | Ingredients | Food, meals | No |
| **Dyeing** | Dye Table | Item + dye | Colored item | Yes — preserves type, changes color |
| **Upgrading** | Smithing Table | Tool/armor + material | Higher-tier item | Partial — preserves enchants |
| **Refining** | Mill / Press | Crops, seeds | Flour, oil, juice | No |

### Where This Fits

The existing `CraftingRegistry` handles the **Workbench** category (shaped and shapeless recipes). This document defines the architecture for all other stations and processes, plus dynamic in-world transformations (e.g., a block transforming when the environment changes).

---

## 2. Core Architecture

### 2.1 The Process Definition

Every transformation is a `ProcessDefinition`: a recipe that describes input ingredients, required station, optional tools, skill requirements, and output.

```typescript
// /src/content/processes/ProcessTypes.ts

/** Types of transformation stations in the world. */
export const enum StationType {
  /** Any 2x2 or 3x3 grid (player inventory or workbench block). */
  WORKBENCH,
  /** Furnace/forge block — requires fuel. */
  FORGE,
  /** Alchemy table block — requires empty bottles/vials. */
  ALCHEMY_TABLE,
  /** Infusion altar block — requires essence gems. */
  INFUSION_ALTAR,
  /** Anvil block — repairs and renames. */
  ANVIL,
  /** Grindstone block — repairs by combining. */
  GRINDSTONE,
  /** Disassembly table — salvages items into materials. */
  DISASSEMBLY_TABLE,
  /** Campfire block — cooks food. */
  CAMPFIRE,
  /** Working furnace/kitchen block — advanced cooking. */
  KITCHEN,
  /** Dye table — colors items. */
  DYE_TABLE,
  /** Smithing table — upgrades tool/armor tier. */
  SMITHING_TABLE,
  /** Wind/water mill — bulk processing. */
  MILL,
  /** No station needed — in-world transformation. */
  IN_WORLD,
}

/** Which inventory slot the output goes into. */
export const enum OutputSlot {
  /** Standard: output in the result slot (like crafting). */
  RESULT_SLOT,
  /** Drop to ground / eject from station. */
  EJECT,
  /** Insert into adjacent inventory / hopper. */
  INSERT_ADJACENT,
}

/** A single ingredient in a process recipe. */
export interface ProcessIngredient {
  /** Item ID of the ingredient. */
  readonly itemId: number;
  /** Quantity required. */
  readonly count: number;
  /** Optional: the ingredient is not consumed (e.g., tools, molds). */
  readonly consumed: boolean;
  /** Optional: minimum durability/metadata value required. */
  readonly minDurability?: number;
  /** Optional: metadata value must match exactly (e.g., wool color). */
  readonly metadata?: number;
}

/** Stat modifier applied by infusion/enchanting. */
export interface StatModifier {
  readonly stat: string;       // e.g., "meleeDamage", "attackSpeed", "maxMana"
  readonly operation: "add" | "multiply" | "percent";
  readonly value: number;
}

/** Outcome of a process — can produce items, apply modifiers, or both. */
export interface ProcessOutput {
  /** The item ID produced. */
  readonly itemId: number;
  /** Quantity produced. */
  readonly count: number;
  /** Optional metadata to set on the output. */
  readonly metadata?: number;
  /** Optional stat modifiers to apply (for infusion/enchanting). */
  readonly modifiers?: readonly StatModifier[];
  /** Optional: probability of this output (0.0-1.0). For randomized processes. */
  readonly chance?: number;
}

/** Complete definition of a transformation process. */
export interface ProcessDefinition {
  /** Unique identifier. */
  readonly id: string;
  /** Display name shown in UI. */
  readonly name: string;
  /** Description of the process. */
  readonly description: string;
  /** Which station this process runs at. */
  readonly station: StationType;
  /** How long the process takes in seconds. */
  readonly processTime: number;
  /** Ingredients required (consumed unless marked otherwise). */
  readonly ingredients: readonly ProcessIngredient[];
  /** Optional: tool item that must be in inventory or in the station. */
  readonly requiredTool?: ProcessIngredient;
  /** Optional: minimum skill level required. */
  readonly skillRequirement?: { readonly skillId: number; readonly level: number };
  /** Optional: minimum character level required. */
  readonly levelRequirement?: number;
  /** Outputs of the process. */
  readonly outputs: readonly ProcessOutput[];
  /** Where the output appears. */
  readonly outputSlot: OutputSlot;
  /** Optional: fuel item ID and burn time in seconds (for FORGE, CAMPFIRE). */
  readonly fuel?: { readonly itemId: number; readonly burnTime: number };
  /** Optional: experience granted for completing this process. */
  readonly experienceReward?: number;
  /** Optional: skill XP granted. */
  readonly skillXpReward?: number;
}
```

### 2.2 Process Registry

```typescript
// /src/content/processes/ProcessRegistry.ts

import type { ProcessDefinition, StationType } from "./ProcessTypes.js";

export class ProcessRegistry {
  private readonly processes = new Map<string, ProcessDefinition>();
  private readonly byStation = new Map<StationType, ProcessDefinition[]>();

  register(process: ProcessDefinition): void {
    if (this.processes.has(process.id)) {
      throw new Error(`Process ${process.id} already registered`);
    }
    this.processes.set(process.id, process);

    const stationList = this.byStation.get(process.station) ?? [];
    stationList.push(process);
    this.byStation.set(process.station, stationList);
  }

  get(id: string): ProcessDefinition | undefined {
    return this.processes.get(id);
  }

  getByStation(station: StationType): readonly ProcessDefinition[] {
    return this.byStation.get(station) ?? [];
  }

  /** Find all processes that can be made from a given set of ingredient IDs. */
  findMatching(
    ingredientIds: readonly number[],
    station: StationType,
    skillLevel?: number,
  ): ProcessDefinition[] {
    const candidates = this.getByStation(station);
    const results: ProcessDefinition[] = [];

    for (const process of candidates) {
      if (skillLevel !== undefined && process.skillRequirement) {
        if (skillLevel < process.skillRequirement.level) continue;
      }

      if (this.ingredientsMatch(process, ingredientIds)) {
        results.push(process);
      }
    }

    return results;
  }

  /** Check whether a set of ingredient IDs satisfies a process's requirements. */
  private ingredientsMatch(process: ProcessDefinition, ingredientIds: readonly number[]): boolean {
    const required = process.ingredients.filter((i) => i.consumed);
    if (required.length !== ingredientIds.length) return false;

    const sortedRequired = [...required].map((i) => i.itemId).sort((a, b) => a - b);
    const sortedProvided = [...ingredientIds].sort((a, b) => a - b);

    for (let i = 0; i < sortedRequired.length; i++) {
      if (sortedRequired[i] !== sortedProvided[i]) return false;
    }

    return true;
  }

  getAll(): readonly ProcessDefinition[] {
    return Array.from(this.processes.values());
  }
}
```

### 2.3 Process Execution Engine

```typescript
// /src/engine/ecs/systems/ProcessExecutionSystem.ts

import type { ProcessRegistry } from "../../../content/processes/ProcessRegistry.js";
import type { ProcessDefinition, ProcessOutput } from "../../../content/processes/ProcessTypes.js";
import { OutputSlot } from "../../../content/processes/ProcessTypes.js";
import type { ComponentStore } from "../ComponentStore.js";
import { InventoryComponentDesc } from "../components/InventoryComponent.js";
import type { DurabilitySystem } from "./DurabilitySystem.js";
import type { SkillSystem } from "./SkillSystem.js";

/**
 * Executes item transformation processes.
 *
 * This system handles the full lifecycle:
 *   1. Validate ingredients are present
 *   2. Check tool durability
 *   3. Check skill requirements
 *   4. Consume ingredients (reduce counts, break tools if durability hits 0)
 *   5. Generate outputs (place in result slot, eject, or insert)
 *   6. Award XP and skill XP
 *   7. Apply stat modifiers (for infusion/enchanting)
 */
export class ProcessExecutionSystem {
  constructor(
    private readonly processRegistry: ProcessRegistry,
    private readonly inventories: ComponentStore<typeof InventoryComponentDesc>,
    private readonly durabilitySystem?: DurabilitySystem,
    private readonly skillSystem?: SkillSystem,
  ) {}

  /**
   * Attempt to execute a process for a player entity.
   *
   * @param process   The process to execute
   * @param playerEntityIndex  Row index of the player in the inventory store
   * @param stationSlotBase    Base slot index in the inventory for the station's input area
   * @param resultSlotIndex    Slot index for the result output
   * @returns true if the process was executed successfully
   */
  execute(
    process: ProcessDefinition,
    playerEntityIndex: number,
    stationSlotBase: number,
    resultSlotIndex: number,
  ): boolean {
    const row = this.inventories.rowFor(playerEntityIndex);
    if (row === -1) return false;

    const ids = this.inventories.data.itemIds;
    const counts = this.inventories.data.itemCounts;
    const meta = this.inventories.data.itemMetadata;
    const base = row * 45;

    // 1. Check that all required ingredients exist in the station slots
    if (!this.checkIngredients(process, ids, counts, base, stationSlotBase)) {
      return false;
    }

    // 2. Check tool durability
    if (process.requiredTool && this.durabilitySystem) {
      // Find the tool, verify durability
      const toolFound = this.findAndDamageTool(process, ids, counts, meta, base, stationSlotBase);
      if (!toolFound) return false;
    }

    // 3. Check the result slot is empty or can stack
    const resultBase = base + resultSlotIndex;
    const outputs = process.outputs;

    // For single-output processes, check the result slot
    if (outputs.length === 1 && process.outputSlot === OutputSlot.RESULT_SLOT) {
      const out = outputs[0];
      if (!this.canAcceptResult(ids, counts, resultBase, out)) return false;
    }

    // 4. Consume ingredients
    this.consumeIngredients(process, ids, counts, meta, base, stationSlotBase);

    // 5. Generate outputs
    this.generateOutputs(process, ids, counts, meta, base, resultBase);

    // 6. Award experience
    if (process.experienceReward) {
      // Forward to LevelingSystem
    }
    if (process.skillXpReward && this.skillSystem) {
      // Forward to SkillSystem
    }

    return true;
  }

  private checkIngredients(
    process: ProcessDefinition,
    ids: Int16Array,
    counts: Uint8Array,
    base: number,
    stationBase: number,
  ): boolean {
    // Build map of required consumed ingredients
    const required = new Map<number, number>();
    for (const ing of process.ingredients) {
      if (ing.consumed) {
        required.set(ing.itemId, (required.get(ing.itemId) ?? 0) + ing.count);
      }
    }

    // Check station slots for each required ingredient
    for (let i = 0; i < 4; i++) { // station is 4 slots (2x2) or 9 slots (3x3)
      const slotBase = base + stationBase + i;
      const itemId = ids[slotBase];
      const count = counts[slotBase];
      if (itemId === 0 || count === 0) continue;

      const needed = required.get(itemId);
      if (needed !== undefined) {
        required.set(itemId, needed - count);
      }
    }

    // All required ingredients must be satisfied (remaining <= 0)
    for (const remaining of required.values()) {
      if (remaining > 0) return false;
    }

    return true;
  }

  private findAndDamageTool(
    process: ProcessDefinition,
    ids: Int16Array,
    counts: Uint8Array,
    meta: Int16Array,
    base: number,
    stationBase: number,
  ): boolean {
    if (!process.requiredTool || !this.durabilitySystem) return true;

    // Look for the tool in the station area or player hotbar
    const searchSlots = [
      ...Array.from({ length: 4 }, (_, i) => base + stationBase + i),
      ...Array.from({ length: 9 }, (_, i) => base + i), // hotbar
    ];

    for (const slot of searchSlots) {
      if (ids[slot] === process.requiredTool.itemId) {
        // Tool found — damage it
        const maxDurability = process.requiredTool.itemId; // lookup from registry
        const shouldBreak = this.durabilitySystem.damage(meta, slot, maxDurability, 1);
        if (shouldBreak) {
          ids[slot] = 0;
          counts[slot] = 0;
          meta[slot] = 0;
        }
        return true;
      }
    }

    return false; // tool not found
  }

  private canAcceptResult(
    ids: Int16Array,
    counts: Uint8Array,
    resultBase: number,
    output: ProcessOutput,
  ): boolean {
    const existingId = ids[resultBase];
    const existingCount = counts[resultBase];

    if (existingId === 0) return true; // empty slot

    // Same item type, check stack limit
    if (existingId === output.itemId) {
      const maxStack = output.itemId < 256 ? 64 : 64; // lookup from registry
      return existingCount + output.count <= maxStack;
    }

    return false;
  }

  private consumeIngredients(
    process: ProcessDefinition,
    ids: Int16Array,
    counts: Uint8Array,
    meta: Int16Array,
    base: number,
    stationBase: number,
  ): void {
    const toConsume = new Map<number, number>();
    for (const ing of process.ingredients) {
      if (ing.consumed) {
        toConsume.set(ing.itemId, (toConsume.get(ing.itemId) ?? 0) + ing.count);
      }
    }

    for (let i = 0; i < 4; i++) {
      const slotBase = base + stationBase + i;
      const itemId = ids[slotBase];
      if (itemId === 0) continue;

      const need = toConsume.get(itemId);
      if (need === undefined) continue;

      const available = counts[slotBase];
      const remove = Math.min(available, need);
      counts[slotBase] -= remove;
      if (counts[slotBase] === 0) {
        ids[slotBase] = 0;
        meta[slotBase] = 0;
      }
      toConsume.set(itemId, need - remove);
    }
  }

  private generateOutputs(
    process: ProcessDefinition,
    ids: Int16Array,
    counts: Uint8Array,
    meta: Int16Array,
    base: number,
    resultBase: number,
  ): void {
    for (const output of process.outputs) {
      // Roll chance
      if (output.chance !== undefined && Math.random() > output.chance) continue;

      if (process.outputSlot === OutputSlot.RESULT_SLOT) {
        if (ids[resultBase] === 0) {
          ids[resultBase] = output.itemId;
          counts[resultBase] = output.count;
          meta[resultBase] = output.metadata ?? 0;
        } else if (ids[resultBase] === output.itemId) {
          counts[resultBase] += output.count;
        }
      } else if (process.outputSlot === OutputSlot.EJECT) {
        // Spawn item entity in the world at the station's position
        // This is handled by a separate ItemEntity spawning system
      }
    }
  }
}
```

---

## 3. Smelting & Fuel System

### 3.1 Furnace Process

The furnace consumes fuel and smelts inputs over time. The `SmeltingSystem` tracks burn progress and item progress per furnace block entity.

```typescript
// /src/engine/ecs/systems/SmeltingSystem.ts

/**
 * Furnace state is stored in a dedicated component attached to furnace
 * block entities in the ECS.
 */
export const FurnaceStateDesc = {
  /** Item ID currently being smelted. */
  smeltingItem: { type: Int16Array, length: 1 },
  /** Item ID of the fuel currently burning. */
  fuelItem: { type: Int16Array, length: 1 },
  /** Progress of the current smelt operation (0.0 - 1.0). */
  smeltProgress: { type: Float32Array, length: 1 },
  /** Remaining burn time of the current fuel (0.0 - 1.0). */
  burnProgress: { type: Float32Array, length: 1 },
  /** Total burn time of the current fuel in seconds. */
  totalBurnTime: { type: Float32Array, length: 1 },
  /** Whether the furnace is currently lit. */
  isLit: { type: Uint8Array, length: 1 },
} as const satisfies ComponentDesc;

export const enum FuelTier {
  /** 10 seconds — sticks, saplings, bamboo. */
  WEAK = 10,
  /** 15 seconds — planks, wood slabs. */
  STANDARD = 15,
  /** 40 seconds — logs, coal. */
  GOOD = 40,
  /** 80 seconds — coal blocks, lava buckets. */
  EXCELLENT = 80,
  /** 160 seconds — blaze rods. */
  SUPERIOR = 160,
}

/**
 * Smelting happens over time. Each game tick:
 *
 *   1. If fuelRunning && hasSmeltableInput → advance smeltProgress
 *   2. If smeltProgress >= 1.0 → convert input to output, reset smeltProgress
 *   3. If fuelRunning → decrement burnProgress
 *   4. If burnProgress <= 0 → consume next fuel item, reset burnProgress
 *   5. If no fuel and smeltProgress > 0 → pause smelting (progress does not decay)
 */
export class SmeltingSystem {
  private static readonly SMELT_TIME = 8; // seconds to smelt one item
  private static readonly FUEL_TIERS: Record<number, FuelTier> = {
    // Item ID → fuel tier mapping
    280: FuelTier.WEAK,      // Stick
    5: FuelTier.STANDARD,    // Planks
    17: FuelTier.GOOD,       // Log
    263: FuelTier.GOOD,      // Coal
    173: FuelTier.EXCELLENT, // Coal Block
    327: FuelTier.EXCELLENT, // Lava Bucket
    360: FuelTier.SUPERIOR,  // Blaze Rod
  };

  update(dt: number): void {
    // Iterate all furnace entities in loaded chunks
    // Advance timers, consume fuel, convert inputs
  }

  /** Get the burn time in seconds for a given fuel item. */
  static getBurnTime(itemId: number): number {
    return SmeltingSystem.FUEL_TIERS[itemId] ?? 0;
  }
}
```

### 3.2 Smelting Recipes

```typescript
// /src/content/processes/smelting-recipes.ts

import { StationType, type ProcessDefinition, type ProcessIngredient } from "./ProcessTypes.js";

/**
 * Smelting transforms raw materials into refined ones using heat.
 * The input item is consumed, fuel is consumed separately by the furnace,
 * and the output appears in the furnace's result slot.
 *
 * Smelting always takes 8 seconds regardless of the item (fuel burn rate
 * determines how many items can be smelted per fuel unit).
 */
export const SMELTING_RECIPES: ProcessDefinition[] = [
  // Ore → Ingots
  {
    id: "smelt_iron_ore",
    name: "Smelt Iron Ore",
    description: "Refine iron ore into an iron ingot.",
    station: StationType.FORGE,
    processTime: 8,
    ingredients: [{ itemId: 15, count: 1, consumed: true }], // iron ore
    outputs: [{ itemId: 340, count: 1 }], // iron ingot
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 5,
  },
  {
    id: "smelt_gold_ore",
    name: "Smelt Gold Ore",
    description: "Refine gold ore into a gold ingot.",
    station: StationType.FORGE,
    processTime: 8,
    ingredients: [{ itemId: 14, count: 1, consumed: true }], // gold ore
    outputs: [{ itemId: 341, count: 1 }], // gold ingot
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 10,
  },
  // Food → Cooked Food
  {
    id: "cook_beef",
    name: "Cook Beef",
    description: "Cook raw beef into a steak.",
    station: StationType.CAMPFIRE,
    processTime: 8,
    ingredients: [{ itemId: 582, count: 1, consumed: true }], // raw beef
    outputs: [{ itemId: 581, count: 1 }], // cooked beef
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 3,
  },
  {
    id: "cook_porkchop",
    name: "Cook Porkchop",
    description: "Cook raw porkchop into a cooked porkchop.",
    station: StationType.CAMPFIRE,
    processTime: 8,
    ingredients: [{ itemId: 579, count: 1, consumed: true }], // raw porkchop
    outputs: [{ itemId: 578, count: 1 }], // cooked porkchop
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 3,
  },
  {
    id: "cook_chicken",
    name: "Cook Chicken",
    description: "Cook raw chicken. Removes the hunger effect risk.",
    station: StationType.CAMPFIRE,
    processTime: 8,
    ingredients: [{ itemId: 584, count: 1, consumed: true }], // raw chicken
    outputs: [{ itemId: 583, count: 1 }], // cooked chicken
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 3,
  },
  // Clay → Brick
  {
    id: "fire_brick",
    name: "Fire Brick",
    description: "Fire clay in a furnace to produce a brick.",
    station: StationType.FORGE,
    processTime: 8,
    ingredients: [{ itemId: 357, count: 1, consumed: true }], // clay ball
    outputs: [{ itemId: 358, count: 1 }], // brick
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 2,
  },
  // Glass — from sand
  {
    id: "smelt_glass",
    name: "Smelt Glass",
    description: "Melt sand into glass.",
    station: StationType.FORGE,
    processTime: 8,
    ingredients: [{ itemId: 12, count: 1, consumed: true }], // sand block
    outputs: [{ itemId: 20, count: 1 }], // glass block
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 4,
  },
  // Log → Charcoal
  {
    id: "smelt_charcoal",
    name: "Smelt Charcoal",
    description: "Burn logs in a low-oxygen environment to produce charcoal.",
    station: StationType.FORGE,
    processTime: 8,
    ingredients: [{ itemId: 17, count: 1, consumed: true }], // log block
    outputs: [{ itemId: 263, count: 1 }], // charcoal (same ID system as coal)
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 3,
  },
];
```

---

## 4. Alchemy & Brewing

### 4.1 Alchemy Process

Alchemy transforms base ingredients (plants, essences, vials) into potions, poisons, and oils. Unlike smelting, alchemy is instantaneous per operation (processTime represents the animation duration) and can produce multiple outputs from a single set of ingredients.

```typescript
// /src/content/processes/alchemy-recipes.ts

import { StationType, type ProcessDefinition } from "./ProcessTypes.js";

/**
 * Alchemy recipes use an Alchemy Table station.
 *
 * The alchemy table has:
 *   - 3 ingredient slots (plants, essences, powders)
 *   - 1 reagent slot (water vial, oil base, etc.)
 *   - 1 output slot (the resulting potion/oil)
 *   - 1 catalyst slot (optional — enhances potency or duration)
 *
 * Processes can be learned from recipe scrolls or discovered through
 * experimentation. Unknown combinations produce "Murky Potion" (random effect).
 */
export const ALCHEMY_RECIPES: ProcessDefinition[] = [
  // Healing Potions
  {
    id: "alch_minor_heal",
    name: "Minor Healing Potion",
    description: "Brew a potion that restores 30 HP over 3 seconds.",
    station: StationType.ALCHEMY_TABLE,
    processTime: 2,
    ingredients: [
      { itemId: 700, count: 1, consumed: true }, // water vial (base)
      { itemId: 701, count: 2, consumed: true }, // lifeleaf (plant)
    ],
    outputs: [{ itemId: 710, count: 1 }], // minor healing potion
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 10,
    skillXpReward: 15,
  },
  {
    id: "alch_major_heal",
    name: "Major Healing Potion",
    description: "Brew a potion that restores 80 HP over 5 seconds.",
    station: StationType.ALCHEMY_TABLE,
    processTime: 3,
    ingredients: [
      { itemId: 700, count: 1, consumed: true }, // water vial
      { itemId: 701, count: 3, consumed: true }, // lifeleaf
      { itemId: 702, count: 1, consumed: true }, // crystal shard (catalyst)
    ],
    outputs: [{ itemId: 711, count: 1 }], // major healing potion
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 25,
    skillXpReward: 30,
  },
  // Mana Potions
  {
    id: "alch_mana_potion",
    name: "Mana Potion",
    description: "Brew a potion that restores 50 mana.",
    station: StationType.ALCHEMY_TABLE,
    processTime: 2,
    ingredients: [
      { itemId: 700, count: 1, consumed: true }, // water vial
      { itemId: 703, count: 2, consumed: true }, // mana blossom
    ],
    outputs: [{ itemId: 712, count: 1 }], // mana potion
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 12,
    skillXpReward: 15,
  },
  // Poisons — applied to weapons
  {
    id: "alch_weak_poison",
    name: "Weak Poison Oil",
    description: "Craft a poison oil that adds 5 DPS for 10 seconds on hit.",
    station: StationType.ALCHEMY_TABLE,
    processTime: 2,
    ingredients: [
      { itemId: 704, count: 1, consumed: true }, // oil base
      { itemId: 705, count: 2, consumed: true }, // nightshade
    ],
    outputs: [{ itemId: 720, count: 1 }], // weak poison oil
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 15,
    skillXpReward: 20,
  },
  // Transformation Potions
  {
    id: "alch_swiftness",
    name: "Swiftness Potion",
    description: "Brew a potion that increases movement speed by 40% for 30 seconds.",
    station: StationType.ALCHEMY_TABLE,
    processTime: 2,
    ingredients: [
      { itemId: 700, count: 1, consumed: true }, // water vial
      { itemId: 706, count: 2, consumed: true }, // wind petal
      { itemId: 707, count: 1, consumed: true }, // sugar
    ],
    outputs: [{ itemId: 713, count: 1 }], // swiftness potion
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 10,
    skillXpReward: 12,
  },
  {
    id: "alch_invisibility",
    name: "Invisibility Potion",
    description: "Brew a potion that grants invisibility for 20 seconds.",
    station: StationType.ALCHEMY_TABLE,
    processTime: 4,
    ingredients: [
      { itemId: 700, count: 1, consumed: true }, // water vial
      { itemId: 708, count: 2, consumed: true }, // ghost cap (mushroom)
      { itemId: 709, count: 1, consumed: true }, // prismatic dust
    ],
    outputs: [{ itemId: 714, count: 1 }], // invisibility potion
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 30,
    skillXpReward: 40,
    skillRequirement: { skillId: 22, level: 30 }, // Alchemy skill
  },
];
```

### 4.2 Potion Effect Application

```typescript
// /src/content/items/potions/PotionEffect.ts

/**
 * Potions store their effect data in the item's metadata field.
 *
 * Metadata encoding for potions:
 *   Bits 0-7:   Effect ID (0-255)
 *   Bits 8-15:  Potency/Amplifier (0-255)
 *   Bits 16-23: Duration in seconds (0-255)
 *   Bits 24-31: Reserved for splash/secondary effects
 *
 * This allows up to 256 unique potion effects, each with configurable
 * strength and duration, all packed into a single Int16 per slot.
 */

export const enum PotionEffectId {
  HEALING = 1,
  MANA_RESTORE = 2,
  SWIFTNESS = 3,
  POISON = 4,
  WEAKNESS = 5,
  STRENGTH = 6,
  INVISIBILITY = 7,
  REGENERATION = 8,
  SLOWNESS = 9,
  HASTE = 10,
  NIGHT_VISION = 11,
  WATER_BREATHING = 12,
  FIRE_RESISTANCE = 13,
}

export function packPotionMeta(effectId: number, amplifier: number, duration: number): number {
  return (effectId & 0xff) | ((amplifier & 0xff) << 8) | ((duration & 0xff) << 16);
}

export function unpackPotionMeta(meta: number): {
  effectId: number;
  amplifier: number;
  duration: number;
} {
  return {
    effectId: meta & 0xff,
    amplifier: (meta >> 8) & 0xff,
    duration: (meta >> 16) & 0xff,
  };
}
```

---

## 5. Infusion & Enchanting

### 5.1 Infusion Process

Infusion imbues an existing item with magical properties using essence gems and catalysts at an Infusion Altar. Unlike crafting, infusion preserves the base item and adds modifiers.

```typescript
// /src/content/processes/infusion-recipes.ts

import { StationType, type ProcessDefinition } from "./ProcessTypes.js";

/**
 * Infusion recipes consume essence gems and optionally catalysts,
 * applying stat modifiers to the target item.
 *
 * The infusion altar has:
 *   - 1 target slot (the item to be infused)
 *   - 2 essence slots (gems that provide modifiers)
 *   - 1 catalyst slot (optional — boosts potency)
 *   - 1 output slot (the infused item)
 *
 * Key difference from crafting: the target item's metadata (durability,
 * existing modifiers) is preserved, and new modifiers are merged on top.
 */

export const INFUSION_RECIPES: ProcessDefinition[] = [
  {
    id: "infuse_flame",
    name: "Infuse Flame",
    description: "Imbue a weapon with fire essence, adding flame damage on hit.",
    station: StationType.INFUSION_ALTAR,
    processTime: 3,
    ingredients: [
      { itemId: 500, count: 1, consumed: false }, // target weapon (not consumed)
      { itemId: 730, count: 2, consumed: true },   // fire essence gem
    ],
    outputs: [{
      itemId: 500,     // same item ID — modifiers change behavior, not identity
      count: 1,
      modifiers: [
        { stat: "fireDamage", operation: "add", value: 8 },
        { stat: "meleeDamage", operation: "add", value: 2 },
      ],
    }],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 30,
    skillXpReward: 40,
    skillRequirement: { skillId: 23, level: 15 }, // Enchanting skill
  },
  {
    id: "infuse_frost",
    name: "Infuse Frost",
    description: "Imbue a weapon with ice essence, slowing enemies on hit.",
    station: StationType.INFUSION_ALTAR,
    processTime: 3,
    ingredients: [
      { itemId: 500, count: 1, consumed: false },
      { itemId: 731, count: 2, consumed: true },   // ice essence gem
    ],
    outputs: [{
      itemId: 500,
      count: 1,
      modifiers: [
        { stat: "frostDamage", operation: "add", value: 6 },
        { stat: "attackSpeed", operation: "multiply", value: 0.9 },
      ],
    }],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 30,
    skillXpReward: 40,
  },
  {
    id: "infuse_protection",
    name: "Infuse Protection",
    description: "Imbue armor with protective wards, reducing incoming damage.",
    station: StationType.INFUSION_ALTAR,
    processTime: 3,
    ingredients: [
      { itemId: 512, count: 1, consumed: false },  // any armor piece
      { itemId: 732, count: 2, consumed: true },   // protection essence
    ],
    outputs: [{
      itemId: 512,
      count: 1,
      modifiers: [
        { stat: "armor", operation: "add", value: 3 },
        { stat: "toughness", operation: "add", value: 1 },
      ],
    }],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 25,
    skillXpReward: 35,
  },
  {
    id: "infuse_mana",
    name: "Infuse Mana",
    description: "Imbue a piece of equipment with mana essence, increasing mana pool.",
    station: StationType.INFUSION_ALTAR,
    processTime: 2,
    ingredients: [
      { itemId: 500, count: 1, consumed: false },
      { itemId: 733, count: 1, consumed: true },   // mana essence
    ],
    outputs: [{
      itemId: 500,
      count: 1,
      modifiers: [
        { stat: "maxMana", operation: "add", value: 20 },
        { stat: "manaRegen", operation: "add", value: 0.5 },
      ],
    }],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 20,
    skillXpReward: 25,
  },
];
```

### 5.2 Modifier Storage

Infusion modifiers are stored in a dedicated component attached to the item entity (when items are entities on the ground) or serialized into the inventory metadata extension area.

```typescript
// /src/engine/ecs/components/ItemModifierComponent.ts

/**
 * Stores magical modifiers applied to an item through infusion/enchanting.
 * Attached to item entities or referenced from inventory metadata.
 *
 * For inventory storage, modifiers are packed into the metadata field
 * using a bit-packed scheme, or stored in an auxiliary array indexed
 * by the slot position.
 */
export const ItemModifierDesc = {
  // Up to 4 modifier slots per item
  modifierCount: { type: Uint8Array, length: 1 },
  modifierStat0: { type: Uint16Array, length: 1 }, // stat enum ID
  modifierOp0:  { type: Uint8Array,  length: 1 },  // 0=add, 1=multiply, 2=percent
  modifierVal0: { type: Float32Array, length: 1 },
  modifierStat1: { type: Uint16Array, length: 1 },
  modifierOp1:  { type: Uint8Array,  length: 1 },
  modifierVal1: { type: Float32Array, length: 1 },
  modifierStat2: { type: Uint16Array, length: 1 },
  modifierOp2:  { type: Uint8Array,  length: 1 },
  modifierVal2: { type: Float32Array, length: 1 },
  modifierStat3: { type: Uint16Array, length: 1 },
  modifierOp3:  { type: Uint8Array,  length: 1 },
  modifierVal3: { type: Float32Array, length: 1 },
} as const satisfies ComponentDesc;
```

---

## 6. Repair System

### 6.1 Anvil Repair

Repair restores durability to a damaged tool, weapon, or armor piece using the material it's made from.

```typescript
// /src/content/processes/repair-recipes.ts

import { StationType, type ProcessDefinition, type StatModifier } from "./ProcessTypes.js";

/**
 * Repair recipes are generated dynamically based on the item's material.
 * Each repair restores 25% of max durability per material unit consumed.
 *
 * The anvil has:
 *   - 1 target slot (damaged item)
 *   - 1 material slot (ingot, plank, gem, etc.)
 *   - 1 output slot (repaired item)
 *
 * Metadata flow:
 *   input:  metadata = remaining durability (e.g., 42 / 250 for iron pickaxe)
 *   output: metadata = min(input + maxDurability * 0.25, maxDurability)
 */

// Dynamic recipe generator for anvil repair
export function generateRepairProcess(
  itemId: number,
  maxDurability: number,
  repairMaterialId: number,
  repairAmount: number, // durability restored per material unit
): ProcessDefinition {
  return {
    id: `repair_${itemId}`,
    name: `Repair Item ${itemId}`,
    description: "Restore durability using raw materials.",
    station: StationType.ANVIL,
    processTime: 2,
    ingredients: [
      { itemId, count: 1, consumed: false, minDurability: 1 },  // damaged item
      { itemId: repairMaterialId, count: 1, consumed: true },     // repair material
    ],
    outputs: [{ itemId, count: 1, metadata: repairAmount }], // metadata = restored durability
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 5,
  };
}

// Example repair costs:
const REPAIR_COSTS: Record<number, { materialId: number; amountPerUnit: number }> = {
  // Wooden tools — planks restore 15 durability
  256: { materialId: 5, amountPerUnit: 15 },  // wooden pickaxe → planks
  272: { materialId: 5, amountPerUnit: 15 },  // wooden axe → planks
  288: { materialId: 5, amountPerUnit: 15 },  // wooden shovel → planks
  304: { materialId: 5, amountPerUnit: 15 },  // wooden hoe → planks
  // Stone tools — cobblestone restores 33 durability
  257: { materialId: 4, amountPerUnit: 33 },
  273: { materialId: 4, amountPerUnit: 33 },
  289: { materialId: 4, amountPerUnit: 33 },
  305: { materialId: 4, amountPerUnit: 33 },
  // Iron tools — iron ingot restores 63 durability
  258: { materialId: 340, amountPerUnit: 63 },
  274: { materialId: 340, amountPerUnit: 63 },
  290: { materialId: 340, amountPerUnit: 63 },
  306: { materialId: 340, amountPerUnit: 63 },
  // Diamond tools — diamond restores 390 durability
  260: { materialId: 342, amountPerUnit: 390 },
  276: { materialId: 342, amountPerUnit: 390 },
  292: { materialId: 342, amountPerUnit: 390 },
  308: { materialId: 342, amountPerUnit: 390 },
};
```

### 6.2 Grindstone — Combining Repairs

The grindstone allows combining two identical damaged items into one item with the sum of their remaining durability (capped at max).

```typescript
// /src/content/processes/grindstone-recipes.ts

/**
 * Grindstone repair: two identical damaged items combine.
 *
 *   outputDurability = min(input1.durability + input2.durability + maxDurability * 0.05, maxDurability)
 *
 * The grindstone has:
 *   - 2 input slots (both must be the same item type, both must have durability < max)
 *   - 1 output slot (the combined item)
 */
export function generateGrindstoneProcess(itemId: number, maxDurability: number): ProcessDefinition {
  return {
    id: `grindstone_${itemId}`,
    name: `Combine ${itemId}`,
    description: "Combine two damaged items to restore durability.",
    station: StationType.GRINDSTONE,
    processTime: 1,
    ingredients: [
      { itemId, count: 1, consumed: true, minDurability: 0 },
      { itemId, count: 1, consumed: true, minDurability: 0 },
    ],
    outputs: [{
      itemId,
      count: 1,
      // metadata is computed by the execution system as:
      // min(maxDurability, input1.meta + input2.meta + maxDurability * 0.05)
      metadata: 0, // placeholder — actual value computed at runtime
    }],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 3,
  };
}
```

---

## 7. Salvage & Disassembly

### 7.1 Salvage Process

Salvaging breaks down a tool, weapon, or armor into its base materials with some loss (conservation of mass — you get less than you put in).

```typescript
// /src/content/processes/salvage-recipes.ts

import { StationType, type ProcessDefinition } from "./ProcessTypes.js";

/**
 * Salvage recipes recover a portion of the materials used to craft an item.
 *
 * Recovery rates:
 *   - 75% of ingots/gems returned for tools and weapons
 *   - 50% of planks/stone for wooden/stone tools
 *   - 100% of leather/wool for armor
 *
 * The disassembly table has:
 *   - 1 input slot (the item to disassemble)
 *   - Up to 3 output slots (materials)
 *   - The item is consumed entirely (even if damaged)
 */

export const SALVAGE_RECIPES: ProcessDefinition[] = [
  // Iron tools → 2 iron ingots (75% of 3)
  {
    id: "salvage_iron_pickaxe",
    name: "Disassemble Iron Pickaxe",
    description: "Break down an iron pickaxe into raw materials.",
    station: StationType.DISASSEMBLY_TABLE,
    processTime: 2,
    ingredients: [{ itemId: 258, count: 1, consumed: true }],
    outputs: [
      { itemId: 340, count: 2, chance: 1.0 }, // iron ingot
      { itemId: 280, count: 1, chance: 0.8 }, // stick (80% chance)
    ],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 5,
    skillXpReward: 8,
  },
  // Diamond tools → 2 diamonds (66% of 3)
  {
    id: "salvage_diamond_pickaxe",
    name: "Disassemble Diamond Pickaxe",
    description: "Break down a diamond pickaxe into raw materials.",
    station: StationType.DISASSEMBLY_TABLE,
    processTime: 3,
    ingredients: [{ itemId: 260, count: 1, consumed: true }],
    outputs: [
      { itemId: 342, count: 2, chance: 1.0 },
      { itemId: 280, count: 1, chance: 0.8 },
    ],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 15,
    skillXpReward: 20,
  },
  // Iron armor → 3-4 iron ingots
  {
    id: "salvage_iron_chestplate",
    name: "Disassemble Iron Chestplate",
    description: "Break down an iron chestplate into raw materials.",
    station: StationType.DISASSEMBLY_TABLE,
    processTime: 3,
    ingredients: [{ itemId: 530, count: 1, consumed: true }], // iron chestplate
    outputs: [
      { itemId: 340, count: 4, chance: 1.0 },
    ],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 12,
    skillXpReward: 15,
  },
  // Wooden tools → 2 planks, 1 stick
  {
    id: "salvage_wood_pickaxe",
    name: "Disassemble Wooden Pickaxe",
    description: "Break down a wooden pickaxe into raw materials.",
    station: StationType.DISASSEMBLY_TABLE,
    processTime: 1,
    ingredients: [{ itemId: 256, count: 1, consumed: true }],
    outputs: [
      { itemId: 5, count: 2, chance: 1.0 },
      { itemId: 280, count: 1, chance: 0.9 },
    ],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 2,
    skillXpReward: 3,
  },
];
```

---

## 8. Upgrading (Tier Advancement)

### 8.1 Smithing Table Upgrades

Upgrading transforms a tool or armor piece from one material tier to a higher one, preserving any enchantments or modifiers.

```typescript
// /src/content/processes/upgrade-recipes.ts

import { StationType, type ProcessDefinition } from "./ProcessTypes.js";

/**
 * Upgrading converts an item from one tier to a higher tier.
 * The new item ID is the higher-tier equivalent of the input.
 *
 * Example mappings:
 *   Iron Pickaxe (258) → Diamond Pickaxe (260) = { iron: 258 + 2 }
 *   Iron Sword (274)   → Diamond Sword (276)   = { iron: 274 + 2 }
 *
 * The smithing table has:
 *   - 1 target slot (the item to upgrade)
 *   - 1 material slot (the higher-tier material)
 *   - 1 output slot (the upgraded item)
 */

// Map of item ID → upgrade material ID
type UpgradePath = Record<number, { materialId: number; resultId: number }>;

/**
 * For tools and weapons:
 *   Wood → Stone: +1 to ID
 *   Stone → Iron: +1 to ID
 *   Iron → Diamond: +2 to ID
 */
export const TOOL_UPGRADE_PATHS: UpgradePath = {
  // Pickaxes
  256: { materialId: 4, resultId: 257 },     // wood → stone (cobble)
  257: { materialId: 340, resultId: 258 },    // stone → iron (iron ingot)
  258: { materialId: 342, resultId: 260 },    // iron → diamond (diamond)
  // Axes
  272: { materialId: 4, resultId: 273 },
  273: { materialId: 340, resultId: 274 },
  274: { materialId: 342, resultId: 276 },
  // Shovels
  288: { materialId: 4, resultId: 289 },
  289: { materialId: 340, resultId: 290 },
  290: { materialId: 342, resultId: 292 },
  // Hoes
  304: { materialId: 4, resultId: 305 },
  305: { materialId: 340, resultId: 306 },
  306: { materialId: 342, resultId: 308 },
};

export function generateUpgradeProcess(
  inputId: number,
  materialId: number,
  resultId: number,
): ProcessDefinition {
  return {
    id: `upgrade_${inputId}_to_${resultId}`,
    name: `Upgrade Item ${inputId}`,
    description: "Upgrade this item to a higher material tier.",
    station: StationType.SMITHING_TABLE,
    processTime: 4,
    ingredients: [
      { itemId: inputId, count: 1, consumed: true },
      { itemId: materialId, count: 1, consumed: true },
    ],
    outputs: [{ itemId: resultId, count: 1 }],
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 20,
    skillXpReward: 25,
  };
}
```

---

## 9. Dyeing & Coloring

### 9.1 Dye Process

Dyeing changes the color metadata of an item without changing its type. This applies to leather armor, wool blocks, glass, and any item with a `colorable` property.

```typescript
// /src/content/processes/dyeing-recipes.ts

import { StationType, type ProcessDefinition } from "./ProcessTypes.js";

/**
 * Dyeing combines an item with a dye to produce a colored version.
 *
 * The color is stored in the item's metadata field:
 *   Bits 0-4:  Red (0-31)
 *   Bits 5-9:  Green (0-31)
 *   Bits 10-14: Blue (0-31)
 *   Bit 15:     Reserved
 *
 * Dye items contain the target color in their definition.
 * When dyed, the metadata is set to the dye's color value.
 *
 * The dye table has:
 *   - 1 input slot (the item to dye)
 *   - 1 dye slot (the dye item)
 *   - 1 output slot (the dyed item)
 */

export const enum DyeColor {
  WHITE = 0,
  ORANGE = 1,
  MAGENTA = 2,
  LIGHT_BLUE = 3,
  YELLOW = 4,
  LIME = 5,
  PINK = 6,
  GRAY = 7,
  LIGHT_GRAY = 8,
  CYAN = 9,
  PURPLE = 10,
  BLUE = 11,
  BROWN = 12,
  GREEN = 13,
  RED = 14,
  BLACK = 15,
}

// Map of dye item ID → color value
const DYE_COLORS: Record<number, number> = {
  350: 0,  // ink sac = black
  351: 14, // rose red = red
  352: 13, // cactus green = green
  353: 11, // lapis lazuli = blue
  // ... additional dyes
};

export function generateDyeProcess(dyeItemId: number): ProcessDefinition {
  const color = DYE_COLORS[dyeItemId];
  return {
    id: `dye_color_${color}`,
    name: `Dye Item`,
    description: "Change the color of a dyeable item.",
    station: StationType.DYE_TABLE,
    processTime: 1,
    ingredients: [
      { itemId: -1, count: 1, consumed: true },  // wildcard: any dyeable item
      { itemId: dyeItemId, count: 1, consumed: true },
    ],
    outputs: [{ itemId: -1, count: 1, metadata: color }], // same item ID, new color
    outputSlot: OutputSlot.RESULT_SLOT,
    experienceReward: 1,
  };
}
```

---

## 10. In-World & Dynamic Transformations

### 10.1 Environmental Transformations

Some transformations happen without a station, triggered by world conditions.

```typescript
// /src/content/processes/dynamic-transforms.ts

import { StationType, type ProcessDefinition } from "./ProcessTypes.js";

/**
 * Dynamic transformations are evaluated by the World system when
 * block or item conditions change.
 *
 * Trigger types:
 *   BLOCK_PLACED_NEARBY — e.g., water next to lava → obsidian
 *   BLOCK_BROKEN — e.g., breaking a furnace drops the block + contents
 *   ITEM_DROPPED_IN_FLUID — e.g., iron ingot dropped in water → cooled ingot entity
 *   ITEM_EXPOSED_TO_HEAT — e.g., items above lava catch fire → destroyed or transformed
 *   ENTITY_DEATH — e.g., mob killed by fire → cooked drops
 *   TIME_OF_DAY — e.g., crops grow at dawn
 *   SEASONAL — e.g., leaves change color in autumn
 */

export const DYNAMIC_TRANSFORMS: ProcessDefinition[] = [
  // Water + Lava → Obsidian (block interaction)
  {
    id: "dynamic_obsidian",
    name: "Water + Lava → Obsidian",
    description: "When water touches lava, the lava turns to obsidian.",
    station: StationType.IN_WORLD,
    processTime: 0, // instant
    ingredients: [
      { itemId: 8, count: 1, consumed: true },  // water source
      { itemId: 10, count: 1, consumed: true }, // lava source
    ],
    outputs: [{ itemId: 49, count: 1 }], // obsidian block
    outputSlot: OutputSlot.EJECT,
    experienceReward: 0,
  },
  // Dirt + Water → Mud (block interaction)
  {
    id: "dynamic_mud",
    name: "Dirt + Water → Mud",
    description: "Dirt adjacent to water becomes mud.",
    station: StationType.IN_WORLD,
    processTime: 5, // 5 seconds
    ingredients: [
      { itemId: 3, count: 1, consumed: true },   // dirt block
      { itemId: 8, count: 1, consumed: false },   // water source (not consumed)
    ],
    outputs: [{ itemId: 11, count: 1 }], // mud block
    outputSlot: OutputSlot.EJECT,
  },
];

/**
 * In-world transformation detection runs as a system that checks
 * block neighbors on each world tick (or on block place/break events).
 *
 * The EventBus is used to subscribe to block change events and
 * evaluate dynamic transformation recipes when relevant blocks
 * are placed or broken near each other.
 */
```

### 10.2 Block Break Drops

When a block is broken, its drop table can specify different outputs based on the tool used — this is another form of item transformation.

```typescript
// /src/content/loot/block-drop-tables.ts

/**
 * Block break drops define what items are produced when a block
 * is destroyed. The drop can vary based on the tool type and
 * harvest level.
 */

export interface BlockDropEntry {
  /** Item ID to drop. */
  readonly itemId: number;
  /** Minimum count. */
  readonly minCount: number;
  /** Maximum count. */
  readonly maxCount: number;
  /** Tool type required to get this drop (optional). */
  readonly requiredToolType?: number; // ToolType enum
  /** Minimum harvest level required. */
  readonly requiredHarvestLevel?: number;
  /** Fortune multiplier: additional rolls per fortune level. */
  readonly fortuneMultiplier?: number;
  /** Silktouch: if true, drops the block itself instead. */
  readonly silkTouchOverride?: number; // item ID when silk touch is used
}

// Examples:
const BLOCK_DROPS: Record<number, BlockDropEntry[]> = {
  // Coal ore → coal (item 263)
  16: [
    { itemId: 263, minCount: 1, maxCount: 1, fortuneMultiplier: 1 }, // coal
  ],
  // Iron ore → iron ore (silk touch) OR nothing (needs pickaxe)
  15: [
    { itemId: 15, minCount: 1, maxCount: 1, requiredToolType: 0, silkTouchOverride: 15 }, // iron ore with silk touch
    { itemId: 340, minCount: 1, maxCount: 1, requiredHarvestLevel: 1 }, // iron ingot with proper tool
  ],
  // Diamond ore → diamond
  56: [
    { itemId: 56, minCount: 1, maxCount: 1, silkTouchOverride: 56 },
    { itemId: 342, minCount: 1, maxCount: 1, requiredHarvestLevel: 2, fortuneMultiplier: 1 },
  ],
  // Gravel → gravel OR flint
  13: [
    { itemId: 13, minCount: 1, maxCount: 1, silkTouchOverride: 13 },
    { itemId: 355, minCount: 1, maxCount: 1, chance: 0.1 }, // 10% flint
    { itemId: 13, minCount: 1, maxCount: 1, chance: 0.9 }, // 90% gravel
  ],
};
```

---

## 11. Process UI Integration

### 11.1 Station UI Pattern

Each station opens a specific UI layout when the player right-clicks it. The layout defines:
- Input slot positions
- Output slot position
- Fuel slot (for forge/campfire)
- Progress indicator
- List of available recipes (auto-detected from inventory)

```typescript
// /src/ui/station/StationUI.ts

/**
 * Generic station UI that adapts to the station type.
 *
 * Layouts:
 *   FORGE:          [Input][Fuel] | [Progress Bar] | [Output]
 *   ALCHEMY_TABLE:  [Plant][Plant][Plant] | [Vial] | [Catalyst] | [Output]
 *   INFUSION_ALTAR: [Target] | [Essence][Essence] | [Catalyst] | [Output]
 *   ANVIL:          [Target][Material] | [Output]
 *   DISASSEMBLY:    [Input] | [Output][Output][Output]
 *   DYE_TABLE:      [Item][Dye] | [Output]
 *   SMITHING:       [Target][Material] | [Output]
 *   WORKBENCH:      3x3 grid | [Output]
 */

export interface StationLayout {
  readonly stationType: StationType;
  readonly inputSlots: number;
  readonly fuelSlots: number;
  readonly outputSlots: number;
  readonly hasProgressBar: boolean;
  readonly inventorySlotBase: number; // which inventory slots this station uses
}
```

### 11.2 Auto-Discovery

When a player opens a station UI, the system scans the player's inventory and the station's input slots, then highlights all valid processes in a recipe book panel. This gives the player clear feedback about what they can make.

---

## 12. Summary of New Files

```
src/content/processes/
├── ProcessTypes.ts              — Core interfaces: ProcessDefinition, ProcessIngredient, ProcessOutput, StationType
├── ProcessRegistry.ts           — Recipe registry with station-based lookup and ingredient matching
├── smelting-recipes.ts          — Forge and campfire smelting/cooking recipes
├── alchemy-recipes.ts           — Alchemy table potion and oil recipes
├── infusion-recipes.ts          — Infusion altar enchanting recipes
├── repair-recipes.ts            — Anvil and grindstone repair definitions
├── salvage-recipes.ts           — Disassembly table salvage definitions
├── upgrade-recipes.ts           — Smithing table tier upgrade definitions
├── dyeing-recipes.ts            — Dye table coloring definitions
├── dynamic-transforms.ts        — In-world environmental transformations
└── block-drop-tables.ts         — Block break drop definitions

src/engine/ecs/systems/
├── ProcessExecutionSystem.ts    — Core process execution engine
├── SmeltingSystem.ts            — Furnace state management, fuel burn, timed processing

src/engine/ecs/components/
├── FurnaceStateComponent.ts     — Furnace burn/smelt progress state
├── ItemModifierComponent.ts     — Infusion/enchantment modifier storage

src/content/items/potions/
├── PotionEffect.ts              — Potion metadata packing/unpacking, effect IDs

src/ui/station/
├── StationUI.ts                 — Generic station UI layout definitions
```

---

## 13. Transformation Categories Summary

| Category | Station | Time | Consumes Input | Changes ID | Preserves Meta | Skill Based |
|:---------|:--------|:----:|:--------------:|:----------:|:--------------:|:-----------:|
| **Crafting** | Workbench | Instant | Yes | Yes | No | No |
| **Smelting** | Forge | 8s | Yes | Yes | No | No |
| **Cooking** | Campfire | 8s | Yes | Yes | No | No |
| **Alchemy** | Alchemy Table | 2-4s | Yes | Yes | No | Yes |
| **Infusion** | Infusion Altar | 2-3s | Yes (target: no) | No | Yes | Yes |
| **Repair** | Anvil | 2s | No (target), Yes (material) | No | Yes | No |
| **Combine** | Grindstone | 1s | Yes (both) | No | Yes | No |
| **Salvage** | Disassembly | 1-3s | Yes | Yes | No | Yes |
| **Upgrade** | Smithing | 4s | Yes | Yes | Partial | No |
| **Dyeing** | Dye Table | 1s | Yes | No | No (meta changes) | No |
| **Dynamic** | In-World | Varies | Yes | Yes | No | No |
| **Block Break** | In-World | 0s | Yes (block) | Yes | No | No |
