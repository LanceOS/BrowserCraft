# Voxel Engine Technical Design Document: Items System

**Version:** 1.0  
**Scope:** Item classification, abstract factories, item metadata/durability, tool mechanics, consumable effects, block/item duality, inventory integration, and interaction system.  
**Architecture Constraints:** Strict TypeScript, Data-Oriented Design, Zero-Garbage-Collection on hot paths, ECS integration, WebGL2 client.

---

## 1. System Overview

The Items System manages everything the player can hold, use, wear, or consume. In the current codebase, items are identified by integer IDs (matching block IDs for placeable blocks) and stored in flat `TypedArray`s within the ECS `InventoryComponent`. The system currently supports basic inventory operations (pickup, place, swap, shift-click) and simple crafting.

This document expands the item model into a full categorical system where each item class (tool, weapon, food, armor, block, etc.) is defined by an abstract factory that captures its base properties, behaviors, and interactions.

### Item Identity Model

```
itemId (Int16, 0-32767)
  ├── Block items (id < 256):   Placeable blocks; item ID == block ID
  ├── Tools & Weapons (256-511): Durability, harvest level, attack damage
  ├── Armor (512-575):           Defense points, slot type, durability
  ├── Food (576-639):            Hunger restoration, saturation, effects
  ├── Ingredients (640-1023):    Crafting materials, no special behavior
  ├── Tools/Weapons sub-ID mapping
  └── ... other categories
```

Items that are also placeable blocks use the same numeric ID as the block definition in `BlockRegistry`. This allows the existing `setBlockIdAt` path to work without translation. Non-block items use IDs outside the block range.

### Current Codebase Context

The engine already supports:

- **45-slot inventory** (9 hotbar + 27 main + 4 armor + 4 crafting + 1 output) as `Int16Array` item IDs with `Uint8Array` counts and `Int16Array` metadata.
- **Left/right-click inventory interaction** with shift-click transfer, stack splitting, and crafting grid evaluation.
- **Crafting system** with shaped and shapeless recipes.
- **Block placement** by mapping item ID to block ID (same numeric range for blocks).
- **Metadata field** per slot, currently used for crafting metadata but extensible to tool damage, wool color, etc.

---

## 2. Item Classification & Abstract Factories

Every item belongs to a *category* (tool, weapon, food, armor, material, block, etc.). Each category is defined by an abstract factory interface that captures the base properties and behaviors shared by all items in that category.

### 2.1 Base Item Properties

```typescript
// /src/content/items/ItemTypes.ts

/** Maximum stack size for different item categories. */
export const enum MaxStackSize {
  DEFAULT = 64,
  TOOL = 1,
  ARMOR = 1,
  FOOD = 64,
  BLOCK = 64,
  INGREDIENT = 64,
}

/** Rarity tier — affects tooltip color and enchantability. */
export const enum Rarity {
  COMMON,
  UNCOMMON,
  RARE,
  EPIC,
}

/** Categories the item belongs to for registry lookup. */
export const enum ItemCategory {
  BLOCK,
  TOOL,
  WEAPON,
  ARMOR,
  FOOD,
  INGREDIENT,
  CONSUMABLE,
  INTERACTABLE,
  MATERIAL,
}

/** How the item interacts with the world when right-clicked. */
export const enum UseAction {
  /** No action — just selects the item. */
  NONE,
  /** Places a block (block items). */
  PLACE_BLOCK,
  /** Eats the item (food). */
  EAT,
  /** Swings the item (tools/weapons). */
  SWING,
  /** Drinks the item (potions). */
  DRINK,
  /** Uses the item on a target block (hoe, bone meal, etc.). */
  USE_ON_BLOCK,
  /** Uses the item on an entity (lead, name tag, etc.). */
  USE_ON_ENTITY,
  /** Charges then releases (bow, fishing rod). */
  CHARGE_AND_RELEASE,
}

/** Base properties shared by all items. */
export interface ItemProperties {
  /** Numeric item ID (0 = empty/air). */
  readonly id: number;
  /** Human-readable identifier (e.g., "diamond_pickaxe"). */
  readonly name: string;
  /** Display name (e.g., "Diamond Pickaxe"). */
  readonly displayName: string;
  /** Category for registry grouping. */
  readonly category: ItemCategory;
  /** Maximum stack size (1 for tools/armor, 64 for blocks/materials). */
  readonly maxStackSize: number;
  /** Rarity for UI display. */
  readonly rarity: Rarity;
  /** Texture layer index in the item atlas. */
  readonly iconTextureLayer: number;
  /** What happens when the player right-clicks with this item. */
  readonly useAction: UseAction;
}
```

### 2.2 Abstract Factory Interface

```typescript
// /src/content/items/ItemFactory.ts

import type { ItemProperties } from "./ItemTypes.js";

/**
 * Abstract factory for a category of items.
 * Each concrete implementation registers its items and exposes
 * its property table for use by gameplay systems (crafting,
 * interaction, rendering).
 */
export interface ItemCategoryFactory {
  /** Register all items in this category. Returns the assigned ID range. */
  registerItems(registry: ItemRegistry): void;

  /** Return the ItemProperties for a given item ID within this category. */
  getProperties(itemId: number): ItemProperties | undefined;

  /** Return all item definitions in this category. */
  getAllProperties(): readonly ItemProperties[];

  /** The category name (e.g., "tools", "armor", "food"). */
  readonly categoryName: string;
}
```

### 2.3 Item Registry

The `ItemRegistry` is the composition root for all items. It aggregates category factories and provides fast lookup by item ID.

```typescript
// /src/content/items/ItemRegistry.ts

import type { ItemCategoryFactory, ItemProperties } from "./ItemTypes.js";

export class ItemRegistry {
  private readonly categories = new Map<string, ItemCategoryFactory>();
  private readonly byId = new Map<number, ItemProperties>();

  registerCategory(factory: ItemCategoryFactory): void {
    this.categories.set(factory.categoryName, factory);
    factory.registerItems(this);
  }

  /** Called by factories to register individual items. */
  register(props: ItemProperties): void {
    if (this.byId.has(props.id)) {
      throw new Error(`Item ID ${props.id} already registered`);
    }
    this.byId.set(props.id, props);
  }

  getProperties(itemId: number): ItemProperties | undefined {
    return this.byId.get(itemId);
  }

  getCategory(name: string): ItemCategoryFactory | undefined {
    return this.categories.get(name);
  }

  isRegistered(itemId: number): boolean {
    return this.byId.has(itemId);
  }

  getAllItems(): readonly ItemProperties[] {
    return Array.from(this.byId.values());
  }
}
```

---

## 3. Concrete Item Categories

### 3.1 Tools (Pickaxe, Axe, Shovel, Hoe)

Tools have durability, a harvest level that determines which blocks they can break, a base attack damage when used as weapons, and a mining speed multiplier.

```typescript
// /src/content/items/ToolFactory.ts

import type { ItemCategoryFactory, ItemProperties } from "./ItemTypes.js";
import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";

/** Material tier for tools — determines durability, harvest level, and speed. */
export const enum ToolMaterial {
  WOOD = 0,
  STONE = 1,
  IRON = 2,
  GOLD = 3,
  DIAMOND = 4,
  NETHERITE = 5,
}

/** Tool type — determines which blocks it can effectively mine. */
export const enum ToolType {
  PICKAXE,
  AXE,
  SHOVEL,
  HOE,
  /** Not a standard tool; used by shears. */
  SHEARS,
}

export interface ToolProperties extends ItemProperties {
  readonly toolType: ToolType;
  readonly toolMaterial: ToolMaterial;
  /** Maximum durability before the tool breaks. */
  readonly maxDurability: number;
  /** Harvest level: 0=wood, 1=stone, 2=iron, 3=diamond. */
  readonly harvestLevel: number;
  /** Mining speed multiplier (wood=2, stone=4, iron=6, diamond=8, gold=12). */
  readonly miningSpeed: number;
  /** Base attack damage when used as a weapon. */
  readonly attackDamage: number;
  /** Base attack speed. */
  readonly attackSpeed: number;
  /** Block IDs this tool is effective against (for efficiency). */
  readonly effectiveBlocks: readonly number[];
  /** Block IDs this tool is the proper tool for (required to drop items). */
  readonly requiredBlocks: readonly number[];
}

const TOOL_MATERIAL_STATS: Record<ToolMaterial, {
  maxDurability: number;
  harvestLevel: number;
  miningSpeed: number;
  attackDamage: number;      // base damage for the tool type
  enchantability: number;
}> = {
  [ToolMaterial.WOOD]:    { maxDurability: 59,   harvestLevel: 0, miningSpeed: 2,  attackDamage: 0, enchantability: 15 },
  [ToolMaterial.STONE]:   { maxDurability: 131,  harvestLevel: 1, miningSpeed: 4,  attackDamage: 1, enchantability: 5 },
  [ToolMaterial.IRON]:    { maxDurability: 250,  harvestLevel: 2, miningSpeed: 6,  attackDamage: 2, enchantability: 14 },
  [ToolMaterial.GOLD]:    { maxDurability: 32,   harvestLevel: 0, miningSpeed: 12, attackDamage: 0, enchantability: 22 },
  [ToolMaterial.DIAMOND]: { maxDurability: 1561, harvestLevel: 3, miningSpeed: 8,  attackDamage: 3, enchantability: 10 },
  [ToolMaterial.NETHERITE]: { maxDurability: 2031, harvestLevel: 4, miningSpeed: 9, attackDamage: 4, enchantability: 15 },
};

const TOOL_TYPE_ATTACK_DAMAGE: Record<ToolType, number> = {
  [ToolType.PICKAXE]: 1,
  [ToolType.AXE]:     3,
  [ToolType.SHOVEL]:  0.5,
  [ToolType.HOE]:     0,
  [ToolType.SHEARS]:  0,
};

const TOOL_TYPE_ATTACK_SPEED: Record<ToolType, number> = {
  [ToolType.PICKAXE]: 1.2,
  [ToolType.AXE]:     0.8,
  [ToolType.SHOVEL]:  1.0,
  [ToolType.HOE]:     2.0,
  [ToolType.SHEARS]:  1.0,
};

/** Effective blocks (mining speed bonus) per tool type. */
const TOOL_EFFECTIVE_BLOCKS: Record<ToolType, readonly number[]> = {
  [ToolType.PICKAXE]: [1, 4, 7, 14, 15, 16, 21, 22, 23, 41, 42, 45, 48, 49, 56, 57, 73, 89],
  [ToolType.AXE]:     [5, 17, 58],
  [ToolType.SHOVEL]:  [2, 3, 12, 13],
  [ToolType.HOE]:     [2, 3],
  [ToolType.SHEARS]:  [18, 150, 151, 152],
};

/** Required blocks (block breaks without dropping items) per harvest level. */
// harvest level 0: wood (stone, dirt, sand, etc.)
// harvest level 1: stone (coal ore, iron ore, stone, cobblestone)
// harvest level 2: iron (gold ore, diamond ore, redstone ore, lapis ore)
// harvest level 3: diamond (obsidian)

const TOOL_BASE_IDS: Record<ToolType, number> = {
  [ToolType.PICKAXE]: 256,
  [ToolType.AXE]:     272,
  [ToolType.SHOVEL]:  288,
  [ToolType.HOE]:     304,
  [ToolType.SHEARS]:  320,
};

export class ToolFactory implements ItemCategoryFactory {
  readonly categoryName = "tools";

  registerItems(registry: ItemRegistry): void {
    const materials: ToolMaterial[] = [
      ToolMaterial.WOOD, ToolMaterial.STONE, ToolMaterial.IRON,
      ToolMaterial.GOLD, ToolMaterial.DIAMOND,
    ];
    const materialNames: Record<ToolMaterial, string> = {
      [ToolMaterial.WOOD]: "wooden", [ToolMaterial.STONE]: "stone",
      [ToolMaterial.IRON]: "iron", [ToolMaterial.GOLD]: "golden",
      [ToolMaterial.DIAMOND]: "diamond", [ToolMaterial.NETHERITE]: "netherite",
    };
    const toolNames: Record<ToolType, string> = {
      [ToolType.PICKAXE]: "pickaxe", [ToolType.AXE]: "axe",
      [ToolType.SHOVEL]: "shovel", [ToolType.HOE]: "hoe",
      [ToolType.SHEARS]: "shears",
    };

    // Register each material × tool combination
    for (const toolType of [ToolType.PICKAXE, ToolType.AXE, ToolType.SHOVEL, ToolType.HOE] as const) {
      for (const material of materials) {
        if (material === ToolMaterial.NETHERITE) continue; // future
        const baseId = TOOL_BASE_IDS[toolType];
        const materialOffset = material * 4; // 4 IDs per tool type
        const id = baseId + materialOffset;
        const stats = TOOL_MATERIAL_STATS[material];
        const typeDamage = TOOL_TYPE_ATTACK_DAMAGE[toolType];

        registry.register({
          id,
          name: `${materialNames[material]}_${toolNames[toolType]}`,
          displayName: `${materialNames[material].charAt(0).toUpperCase() + materialNames[material].slice(1)} ${toolNames[toolType].charAt(0).toUpperCase() + toolNames[toolType].slice(1)}`,
          category: ItemCategory.TOOL,
          maxStackSize: MaxStackSize.TOOL,
          rarity: material >= ToolMaterial.DIAMOND ? Rarity.RARE : Rarity.COMMON,
          iconTextureLayer: id, // 1:1 mapping to texture array layer
          useAction: UseAction.SWING,
          toolType,
          toolMaterial: material,
          maxDurability: stats.maxDurability,
          harvestLevel: stats.harvestLevel,
          miningSpeed: stats.miningSpeed,
          attackDamage: stats.attackDamage + typeDamage,
          attackSpeed: TOOL_TYPE_ATTACK_SPEED[toolType],
          effectiveBlocks: TOOL_EFFECTIVE_BLOCKS[toolType],
          requiredBlocks: [],
        } satisfies ToolProperties);
      }
    }

    // Shears (single item, not per-material)
    registry.register({
      id: 320,
      name: "shears",
      displayName: "Shears",
      category: ItemCategory.TOOL,
      maxStackSize: MaxStackSize.TOOL,
      rarity: Rarity.COMMON,
      iconTextureLayer: 320,
      useAction: UseAction.SWING,
      toolType: ToolType.SHEARS,
      toolMaterial: ToolMaterial.IRON,
      maxDurability: 238,
      harvestLevel: 0,
      miningSpeed: 1.5,
      attackDamage: 0,
      attackSpeed: 2.0,
      effectiveBlocks: TOOL_EFFECTIVE_BLOCKS[ToolType.SHEARS],
      requiredBlocks: [],
    } satisfies ToolProperties);
  }

  getProperties(itemId: number): ToolProperties | undefined {
    // In production, this would look up from an internal map populated during registration.
    return undefined; // stub — full implementation uses a Map<number, ToolProperties>
  }

  getAllProperties(): ToolProperties[] {
    return []; // stub
  }
}
```

### 3.2 Weapons (Sword, Bow, Arrow)

Weapons extend tools with attack-specific properties. Swords have durability, attack damage, and attack speed. Bows have charge time and arrow velocity. Arrows are ammunition.

```typescript
// /src/content/items/WeaponFactory.ts

import type { ItemProperties } from "./ItemTypes.js";
import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";
import { ToolMaterial } from "./ToolFactory.js";

export const enum WeaponType {
  SWORD,
  BOW,
  ARROW,
}

export interface WeaponProperties extends ItemProperties {
  readonly weaponType: WeaponType;
  readonly toolMaterial?: ToolMaterial; // for swords
  readonly maxDurability: number;
  readonly attackDamage: number;
  readonly attackSpeed: number;
  /** For bows: charge time in ticks (default 20 = 1 second). */
  readonly chargeTime?: number;
  /** For bows: arrow velocity multiplier. */
  readonly arrowVelocity?: number;
}

const WEAPON_BASE_IDS: Record<WeaponType, number> = {
  [WeaponType.SWORD]: 256 + 16,  // 272-287 (after pickaxes: 256-271)
  [WeaponType.BOW]:   300,
  [WeaponType.ARROW]: 301,
};

const SWORD_STATS: Record<ToolMaterial, { attackDamage: number; attackSpeed: number; maxDurability: number }> = {
  [ToolMaterial.WOOD]:    { attackDamage: 4, attackSpeed: 1.6, maxDurability: 59 },
  [ToolMaterial.STONE]:   { attackDamage: 5, attackSpeed: 1.6, maxDurability: 131 },
  [ToolMaterial.IRON]:    { attackDamage: 6, attackSpeed: 1.6, maxDurability: 250 },
  [ToolMaterial.GOLD]:    { attackDamage: 4, attackSpeed: 1.6, maxDurability: 32 },
  [ToolMaterial.DIAMOND]: { attackDamage: 7, attackSpeed: 1.6, maxDurability: 1561 },
  [ToolMaterial.NETHERITE]: { attackDamage: 8, attackSpeed: 1.6, maxDurability: 2031 },
};

export class WeaponFactory implements ItemCategoryFactory {
  readonly categoryName = "weapons";

  registerItems(registry: ItemRegistry): void {
    // Swords
    const materialNames: Record<ToolMaterial, string> = {
      [ToolMaterial.WOOD]: "wooden", [ToolMaterial.STONE]: "stone",
      [ToolMaterial.IRON]: "iron", [ToolMaterial.GOLD]: "golden",
      [ToolMaterial.DIAMOND]: "diamond", [ToolMaterial.NETHERITE]: "netherite",
    };
    const materials: ToolMaterial[] = [
      ToolMaterial.WOOD, ToolMaterial.STONE, ToolMaterial.IRON,
      ToolMaterial.GOLD, ToolMaterial.DIAMOND,
    ];

    for (let i = 0; i < materials.length; i++) {
      const mat = materials[i];
      const stats = SWORD_STATS[mat];
      const id = WEAPON_BASE_IDS[WeaponType.SWORD] + i;
      registry.register({
        id,
        name: `${materialNames[mat]}_sword`,
        displayName: `${materialNames[mat].charAt(0).toUpperCase() + materialNames[mat].slice(1)} Sword`,
        category: ItemCategory.WEAPON,
        maxStackSize: MaxStackSize.TOOL,
        rarity: mat >= ToolMaterial.DIAMOND ? Rarity.RARE : Rarity.COMMON,
        iconTextureLayer: id,
        useAction: UseAction.SWING,
        weaponType: WeaponType.SWORD,
        toolMaterial: mat,
        maxDurability: stats.maxDurability,
        attackDamage: stats.attackDamage,
        attackSpeed: stats.attackSpeed,
      } satisfies WeaponProperties);
    }

    // Bow
    registry.register({
      id: WEAPON_BASE_IDS[WeaponType.BOW],
      name: "bow",
      displayName: "Bow",
      category: ItemCategory.WEAPON,
      maxStackSize: MaxStackSize.TOOL,
      rarity: Rarity.UNCOMMON,
      iconTextureLayer: WEAPON_BASE_IDS[WeaponType.BOW],
      useAction: UseAction.CHARGE_AND_RELEASE,
      weaponType: WeaponType.BOW,
      maxDurability: 384,
      attackDamage: 0, // damage comes from arrow
      attackSpeed: 0,
      chargeTime: 20,
      arrowVelocity: 3.0,
    } satisfies WeaponProperties);

    // Arrow
    registry.register({
      id: WEAPON_BASE_IDS[WeaponType.ARROW],
      name: "arrow",
      displayName: "Arrow",
      category: ItemCategory.WEAPON,
      maxStackSize: MaxStackSize.DEFAULT,
      rarity: Rarity.COMMON,
      iconTextureLayer: WEAPON_BASE_IDS[WeaponType.ARROW],
      useAction: UseAction.NONE,
      weaponType: WeaponType.ARROW,
      maxDurability: 0,
      attackDamage: 2,
      attackSpeed: 0,
    } satisfies WeaponProperties);
  }

  getProperties(itemId: number): WeaponProperties | undefined {
    return undefined; // stub
  }

  getAllProperties(): WeaponProperties[] {
    return []; // stub
  }
}
```

### 3.3 Armor (Helmet, Chestplate, Leggings, Boots)

Armor provides defense points, has durability, and can be enchanted. Each armor piece covers a different slot.

```typescript
// /src/content/items/ArmorFactory.ts

import type { ItemProperties } from "./ItemTypes.js";
import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";
import { ToolMaterial } from "./ToolFactory.js";

export const enum ArmorSlot {
  HELMET,
  CHESTPLATE,
  LEGGINGS,
  BOOTS,
}

export interface ArmorProperties extends ItemProperties {
  readonly armorSlot: ArmorSlot;
  readonly toolMaterial: ToolMaterial;
  readonly defensePoints: number;
  readonly toughness: number;
  readonly maxDurability: number;
}

const ARMOR_BASE_IDS: Record<ArmorSlot, number> = {
  [ArmorSlot.HELMET]:    512,
  [ArmorSlot.CHESTPLATE]: 528,
  [ArmorSlot.LEGGINGS]:   544,
  [ArmorSlot.BOOTS]:      560,
};

const ARMOR_MATERIAL_STATS: Record<ToolMaterial, {
  defense: [number, number, number, number]; // helmet, chestplate, leggings, boots
  toughness: number;
  durability: [number, number, number, number];
}> = {
  [ToolMaterial.WOOD]:    { defense: [1, 3, 2, 1], toughness: 0, durability: [55, 80, 75, 65] },
  [ToolMaterial.STONE]:   { defense: [2, 5, 4, 2], toughness: 0, durability: [75, 110, 100, 85] },
  [ToolMaterial.IRON]:    { defense: [2, 6, 5, 2], toughness: 0, durability: [165, 240, 225, 195] },
  [ToolMaterial.GOLD]:    { defense: [2, 5, 3, 1], toughness: 0, durability: [55, 80, 75, 65] },
  [ToolMaterial.DIAMOND]: { defense: [3, 8, 6, 3], toughness: 2, durability: [363, 528, 495, 429] },
  [ToolMaterial.NETHERITE]: { defense: [3, 8, 6, 3], toughness: 3, durability: [407, 592, 555, 481] },
};

const ARMOR_SLOT_NAMES: Record<ArmorSlot, string> = {
  [ArmorSlot.HELMET]: "helmet",
  [ArmorSlot.CHESTPLATE]: "chestplate",
  [ArmorSlot.LEGGINGS]: "leggings",
  [ArmorSlot.BOOTS]: "boots",
};

export class ArmorFactory implements ItemCategoryFactory {
  readonly categoryName = "armor";

  registerItems(registry: ItemRegistry): void {
    const materialNames: Record<ToolMaterial, string> = {
      [ToolMaterial.WOOD]: "leather", // leather = wood tier for naming
      [ToolMaterial.STONE]: "chainmail",
      [ToolMaterial.IRON]: "iron",
      [ToolMaterial.GOLD]: "golden",
      [ToolMaterial.DIAMOND]: "diamond",
      [ToolMaterial.NETHERITE]: "netherite",
    };
    const materials: ToolMaterial[] = [
      ToolMaterial.WOOD, ToolMaterial.STONE, ToolMaterial.IRON,
      ToolMaterial.GOLD, ToolMaterial.DIAMOND,
    ];

    for (const material of materials) {
      const stats = ARMOR_MATERIAL_STATS[material];
      for (const slot of [ArmorSlot.HELMET, ArmorSlot.CHESTPLATE, ArmorSlot.LEGGINGS, ArmorSlot.BOOTS] as const) {
        const id = ARMOR_BASE_IDS[slot] + material;
        const defenseIdx = [ArmorSlot.HELMET, ArmorSlot.CHESTPLATE, ArmorSlot.LEGGINGS, ArmorSlot.BOOTS].indexOf(slot);
        registry.register({
          id,
          name: `${materialNames[material]}_${ARMOR_SLOT_NAMES[slot]}`,
          displayName: `${materialNames[material].charAt(0).toUpperCase() + materialNames[material].slice(1)} ${ARMOR_SLOT_NAMES[slot].charAt(0).toUpperCase() + ARMOR_SLOT_NAMES[slot].slice(1)}`,
          category: ItemCategory.ARMOR,
          maxStackSize: MaxStackSize.ARMOR,
          rarity: material >= ToolMaterial.DIAMOND ? Rarity.RARE : Rarity.UNCOMMON,
          iconTextureLayer: id,
          useAction: UseAction.SWING, // right-click equips
          armorSlot: slot,
          toolMaterial: material,
          defensePoints: stats.defense[defenseIdx],
          toughness: stats.toughness,
          maxDurability: stats.durability[defenseIdx],
        } satisfies ArmorProperties);
      }
    }
  }

  getProperties(itemId: number): ArmorProperties | undefined {
    return undefined; // stub
  }

  getAllProperties(): ArmorProperties[] {
    return []; // stub
  }
}
```

### 3.4 Food & Consumables

Food items restore hunger and saturation, and may apply status effects. Consumables include potions, milk buckets, and other single-use items.

```typescript
// /src/content/items/FoodFactory.ts

import type { ItemProperties } from "./ItemTypes.js";
import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";

export interface FoodProperties extends ItemProperties {
  /** Hunger points restored (0-20, each half-drumstick = 1). */
  readonly hungerRestoration: number;
  /** Saturation modifier — determines how long the food keeps hunger full. */
  readonly saturationModifier: number;
  /** Whether the food can be eaten even when hunger is full. */
  readonly alwaysEdible: boolean;
  /** Optional status effect applied on consumption. */
  readonly effects?: readonly FoodEffect[];
}

export interface FoodEffect {
  readonly effectId: number;
  readonly duration: number; // in ticks
  readonly amplifier: number;
  readonly probability: number; // 0.0 - 1.0
}

const enum FoodId {
  APPLE = 576,
  BREAD = 577,
  COOKED_PORKCHOP = 578,
  RAW_PORKCHOP = 579,
  GOLDEN_APPLE = 580,
  COOKED_BEEF = 581,
  RAW_BEEF = 582,
  COOKED_CHICKEN = 583,
  RAW_CHICKEN = 584,
  COOKED_MUTTON = 585,
  RAW_MUTTON = 586,
  BAKED_POTATO = 587,
  POTATO = 588,
  POISONOUS_POTATO = 589,
  CARROT = 590,
  GOLDEN_CARROT = 591,
  PUMPKIN_PIE = 592,
  COOKIE = 593,
  MELON_SLICE = 594,
  ROTTEN_FLESH = 595,
  SPIDER_EYE = 596,
  MUSHROOM_STEW = 597,
  BOWL = 598,
}

const FOOD_DEFS: FoodProperties[] = [
  { id: FoodId.APPLE, name: "apple", displayName: "Apple", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.APPLE, useAction: UseAction.EAT, hungerRestoration: 4, saturationModifier: 0.6, alwaysEdible: false },
  { id: FoodId.BREAD, name: "bread", displayName: "Bread", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.BREAD, useAction: UseAction.EAT, hungerRestoration: 5, saturationModifier: 0.6, alwaysEdible: false },
  { id: FoodId.COOKED_PORKCHOP, name: "cooked_porkchop", displayName: "Cooked Porkchop", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.COOKED_PORKCHOP, useAction: UseAction.EAT, hungerRestoration: 8, saturationModifier: 0.8, alwaysEdible: false },
  { id: FoodId.RAW_PORKCHOP, name: "raw_porkchop", displayName: "Raw Porkchop", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.RAW_PORKCHOP, useAction: UseAction.EAT, hungerRestoration: 3, saturationModifier: 0.3, alwaysEdible: false },
  { id: FoodId.GOLDEN_APPLE, name: "golden_apple", displayName: "Golden Apple", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.RARE, iconTextureLayer: FoodId.GOLDEN_APPLE, useAction: UseAction.EAT, hungerRestoration: 4, saturationModifier: 1.2, alwaysEdible: true, effects: [{ effectId: 10, duration: 600, amplifier: 1, probability: 1.0 }] }, // Regeneration II
  { id: FoodId.COOKED_BEEF, name: "cooked_beef", displayName: "Cooked Beef", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.COOKED_BEEF, useAction: UseAction.EAT, hungerRestoration: 8, saturationModifier: 0.8, alwaysEdible: false },
  { id: FoodId.RAW_BEEF, name: "raw_beef", displayName: "Raw Beef", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.RAW_BEEF, useAction: UseAction.EAT, hungerRestoration: 3, saturationModifier: 0.3, alwaysEdible: false },
  { id: FoodId.COOKED_CHICKEN, name: "cooked_chicken", displayName: "Cooked Chicken", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.COOKED_CHICKEN, useAction: UseAction.EAT, hungerRestoration: 6, saturationModifier: 0.6, alwaysEdible: false },
  { id: FoodId.RAW_CHICKEN, name: "raw_chicken", displayName: "Raw Chicken", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.RAW_CHICKEN, useAction: UseAction.EAT, hungerRestoration: 2, saturationModifier: 0.3, alwaysEdible: false, effects: [{ effectId: 17, duration: 600, amplifier: 0, probability: 0.3 }] }, // Hunger 30%
  { id: FoodId.CARROT, name: "carrot", displayName: "Carrot", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.CARROT, useAction: UseAction.EAT, hungerRestoration: 3, saturationModifier: 0.6, alwaysEdible: false },
  { id: FoodId.GOLDEN_CARROT, name: "golden_carrot", displayName: "Golden Carrot", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.UNCOMMON, iconTextureLayer: FoodId.GOLDEN_CARROT, useAction: UseAction.EAT, hungerRestoration: 6, saturationModifier: 1.2, alwaysEdible: false },
  { id: FoodId.BAKED_POTATO, name: "baked_potato", displayName: "Baked Potato", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.BAKED_POTATO, useAction: UseAction.EAT, hungerRestoration: 5, saturationModifier: 0.6, alwaysEdible: false },
  { id: FoodId.ROTTEN_FLESH, name: "rotten_flesh", displayName: "Rotten Flesh", category: ItemCategory.FOOD, maxStackSize: 64, rarity: Rarity.COMMON, iconTextureLayer: FoodId.ROTTEN_FLESH, useAction: UseAction.EAT, hungerRestoration: 4, saturationModifier: 0.1, alwaysEdible: false, effects: [{ effectId: 17, duration: 600, amplifier: 0, probability: 0.8 }] },
];

export class FoodFactory implements ItemCategoryFactory {
  readonly categoryName = "food";

  private readonly foods = new Map<number, FoodProperties>();

  constructor() {
    for (const def of FOOD_DEFS) {
      this.foods.set(def.id, def);
    }
  }

  registerItems(registry: ItemRegistry): void {
    for (const def of FOOD_DEFS) {
      registry.register(def);
    }
  }

  getProperties(itemId: number): FoodProperties | undefined {
    return this.foods.get(itemId);
  }

  getAllProperties(): FoodProperties[] {
    return FOOD_DEFS;
  }
}
```

### 3.5 Block Items

Block items are the item representation of placeable blocks. They use the same ID as the block and delegate placement to the existing `World.setBlockIdAt` path. Most blocks are simple placeables; some (like doors, beds, torches) have special placement logic.

```typescript
// /src/content/items/BlockItemFactory.ts

import type { ItemProperties } from "./ItemTypes.js";
import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";

/**
 * Block items wrap BlockDefinitions into ItemProperties.
 * The item ID matches the block ID, so no translation is needed.
 */
export class BlockItemFactory implements ItemCategoryFactory {
  readonly categoryName = "blocks";

  constructor(private readonly blocks: BlockRegistry) {}

  registerItems(registry: ItemRegistry): void {
    for (const block of this.blocks.values()) {
      registry.register({
        id: block.id,
        name: block.name,
        displayName: block.name.charAt(0).toUpperCase() + block.name.slice(1).replace(/_/g, " "),
        category: ItemCategory.BLOCK,
        maxStackSize: MaxStackSize.BLOCK,
        rarity: Rarity.COMMON,
        iconTextureLayer: block.textures.top, // use top texture as icon
        useAction: UseAction.PLACE_BLOCK,
      });
    }
  }

  getProperties(itemId: number): ItemProperties | undefined {
    const block = this.blocks.tryGet(itemId);
    if (!block) return undefined;
    return {
      id: block.id,
      name: block.name,
      displayName: block.name.charAt(0).toUpperCase() + block.name.slice(1).replace(/_/g, " "),
      category: ItemCategory.BLOCK,
      maxStackSize: MaxStackSize.BLOCK,
      rarity: Rarity.COMMON,
      iconTextureLayer: block.textures.top,
      useAction: UseAction.PLACE_BLOCK,
    };
  }

  getAllProperties(): ItemProperties[] {
    return this.blocks.values().map((block) => ({
      id: block.id,
      name: block.name,
      displayName: block.name.charAt(0).toUpperCase() + block.name.slice(1).replace(/_/g, " "),
      category: ItemCategory.BLOCK,
      maxStackSize: MaxStackSize.BLOCK,
      rarity: Rarity.COMMON,
      iconTextureLayer: block.textures.top,
      useAction: UseAction.PLACE_BLOCK,
    }));
  }
}
```

### 3.6 Materials & Ingredients

Materials are items used in crafting with no special behaviors. They stack to 64 and have no durability or use action.

```typescript
// /src/content/items/MaterialFactory.ts

import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";
import type { ItemCategoryFactory, ItemProperties } from "./ItemTypes.js";

export interface MaterialDefinition {
  readonly id: number;
  readonly name: string;
  readonly displayName: string;
  readonly textureLayer: number;
  readonly rarity?: Rarity;
}

/**
 * Common materials used in crafting recipes.
 * These items have no special behavior — they exist purely
 * as recipe ingredients or decorative placeables.
 */
const COMMON_MATERIALS: MaterialDefinition[] = [
  // Wood derivatives
  { id: 280, name: "stick", displayName: "Stick", textureLayer: 280 },
  // Ingots & gems
  { id: 340, name: "iron_ingot", displayName: "Iron Ingot", textureLayer: 340 },
  { id: 341, name: "gold_ingot", displayName: "Gold Ingot", textureLayer: 341 },
  { id: 342, name: "diamond", displayName: "Diamond", textureLayer: 342, rarity: Rarity.RARE },
  { id: 343, name: "emerald", displayName: "Emerald", textureLayer: 343, rarity: Rarity.RARE },
  { id: 344, name: "netherite_ingot", displayName: "Netherite Ingot", textureLayer: 344, rarity: Rarity.EPIC },
  // Dusts & compounds
  { id: 345, name: "redstone_dust", displayName: "Redstone Dust", textureLayer: 345 },
  { id: 346, name: "glowstone_dust", displayName: "Glowstone Dust", textureLayer: 346 },
  { id: 347, name: "gunpowder", displayName: "Gunpowder", textureLayer: 347 },
  { id: 348, name: "sugar", displayName: "Sugar", textureLayer: 348 },
  // Dyes
  { id: 350, name: "ink_sac", displayName: "Ink Sac", textureLayer: 350 },
  { id: 351, name: "rose_red", displayName: "Rose Red", textureLayer: 351 },
  { id: 352, name: "cactus_green", displayName: "Cactus Green", textureLayer: 352 },
  { id: 353, name: "lapis_lazuli", displayName: "Lapis Lazuli", textureLayer: 353 },
  // Miscellaneous
  { id: 354, name: "feather", displayName: "Feather", textureLayer: 354 },
  { id: 355, name: "flint", displayName: "Flint", textureLayer: 355 },
  { id: 356, name: "leather", displayName: "Leather", textureLayer: 356 },
  { id: 357, name: "clay_ball", displayName: "Clay Ball", textureLayer: 357 },
  { id: 358, name: "brick", displayName: "Brick", textureLayer: 358 },
  { id: 359, name: "nether_wart", displayName: "Nether Wart", textureLayer: 359 },
  { id: 360, name: "blaze_rod", displayName: "Blaze Rod", textureLayer: 360 },
  { id: 361, name: "blaze_powder", displayName: "Blaze Powder", textureLayer: 361 },
  { id: 362, name: "ender_pearl", displayName: "Ender Pearl", textureLayer: 362, rarity: Rarity.UNCOMMON },
  // Seeds (crop planting)
  { id: 115, name: "wheat_seeds", displayName: "Wheat Seeds", textureLayer: 115 },
  { id: 121, name: "potato", displayName: "Potato", textureLayer: 121 }, // also food
  { id: 131, name: "carrot", displayName: "Carrot", textureLayer: 131 }, // also food
  // Harvested crops
  { id: 116, name: "wheat", displayName: "Wheat", textureLayer: 116 },
];

export class MaterialFactory implements ItemCategoryFactory {
  readonly categoryName = "materials";

  private readonly materials = new Map<number, MaterialDefinition>();

  constructor() {
    for (const def of COMMON_MATERIALS) {
      this.materials.set(def.id, def);
    }
  }

  registerItems(registry: ItemRegistry): void {
    for (const def of COMMON_MATERIALS) {
      registry.register({
        id: def.id,
        name: def.name,
        displayName: def.displayName,
        category: ItemCategory.MATERIAL,
        maxStackSize: MaxStackSize.INGREDIENT,
        rarity: def.rarity ?? Rarity.COMMON,
        iconTextureLayer: def.textureLayer,
        useAction: UseAction.NONE,
      });
    }
  }

  getProperties(itemId: number): ItemProperties | undefined {
    const def = this.materials.get(itemId);
    if (!def) return undefined;
    return {
      id: def.id,
      name: def.name,
      displayName: def.displayName,
      category: ItemCategory.MATERIAL,
      maxStackSize: MaxStackSize.INGREDIENT,
      rarity: def.rarity ?? Rarity.COMMON,
      iconTextureLayer: def.textureLayer,
      useAction: UseAction.NONE,
    };
  }

  getAllProperties(): ItemProperties[] {
    return COMMON_MATERIALS.map((def) => ({
      id: def.id,
      name: def.name,
      displayName: def.displayName,
      category: ItemCategory.MATERIAL,
      maxStackSize: MaxStackSize.INGREDIENT,
      rarity: def.rarity ?? Rarity.COMMON,
      iconTextureLayer: def.textureLayer,
      useAction: UseAction.NONE,
    }));
  }
}
```

### 3.7 Interactables (Special-Purpose Items)

Interactable items have unique right-click behaviors that don't fit into other categories. Examples include buckets, flint & steel, bone meal, compass, clock, map, fishing rod, and shears (when used on entities).

```typescript
// /src/content/items/InteractableFactory.ts

import { ItemCategory, MaxStackSize, Rarity, UseAction } from "./ItemTypes.js";
import type { ItemCategoryFactory, ItemProperties } from "./ItemTypes.js";

export const enum InteractableType {
  /** Places or picks up fluids (bucket). */
  FLUID_CONTAINER,
  /** Ignites blocks (flint & steel, fire charge). */
  IGNITER,
  /** Accelerates growth (bone meal). */
  GROWTH_AGENT,
  /** Tills soil (hoe — placed here if treated as interactable vs tool). */
  TILLER,
  /** Strips logs (axe decorative use). */
  STRIPPER,
  /** Lights blocks (torch, lantern). */
  LIGHT_SOURCE,
  /** Tames/leads entities (lead, name tag). */
  ENTITY_TOOL,
  /** Navigates (compass, clock, map). */
  NAVIGATION,
}

export interface InteractableProperties extends ItemProperties {
  readonly interactableType: InteractableType;
  /** For FLUID_CONTAINER: the fluid block ID (8=water, 10=lava). */
  readonly fluidBlockId?: number;
  /** For FLUID_CONTAINER: whether this container is empty. */
  readonly isEmptyContainer?: boolean;
  /** For GROWTH_AGENT: number of growth stages advanced per use. */
  readonly growthAdvance?: number;
  /** Durability (flint & steel: 64 uses). */
  readonly maxDurability?: number;
}

const INTERACTABLE_DEFS: InteractableProperties[] = [
  {
    id: 325, name: "bucket", displayName: "Bucket",
    category: ItemCategory.INTERACTABLE, maxStackSize: 16, rarity: Rarity.COMMON,
    iconTextureLayer: 325, useAction: UseAction.USE_ON_BLOCK,
    interactableType: InteractableType.FLUID_CONTAINER,
    isEmptyContainer: true,
  },
  {
    id: 326, name: "water_bucket", displayName: "Water Bucket",
    category: ItemCategory.INTERACTABLE, maxStackSize: 1, rarity: Rarity.COMMON,
    iconTextureLayer: 326, useAction: UseAction.USE_ON_BLOCK,
    interactableType: InteractableType.FLUID_CONTAINER,
    fluidBlockId: 8,
    isEmptyContainer: false,
  },
  {
    id: 327, name: "lava_bucket", displayName: "Lava Bucket",
    category: ItemCategory.INTERACTABLE, maxStackSize: 1, rarity: Rarity.UNCOMMON,
    iconTextureLayer: 327, useAction: UseAction.USE_ON_BLOCK,
    interactableType: InteractableType.FLUID_CONTAINER,
    fluidBlockId: 10,
    isEmptyContainer: false,
  },
  {
    id: 328, name: "flint_and_steel", displayName: "Flint and Steel",
    category: ItemCategory.INTERACTABLE, maxStackSize: 1, rarity: Rarity.COMMON,
    iconTextureLayer: 328, useAction: UseAction.USE_ON_BLOCK,
    interactableType: InteractableType.IGNITER,
    maxDurability: 64,
  },
  {
    id: 329, name: "bone_meal", displayName: "Bone Meal",
    category: ItemCategory.INTERACTABLE, maxStackSize: 64, rarity: Rarity.COMMON,
    iconTextureLayer: 329, useAction: UseAction.USE_ON_BLOCK,
    interactableType: InteractableType.GROWTH_AGENT,
    growthAdvance: 1, // advances by 1 stage per use (with randomization)
  },
  {
    id: 330, name: "compass", displayName: "Compass",
    category: ItemCategory.INTERACTABLE, maxStackSize: 1, rarity: Rarity.COMMON,
    iconTextureLayer: 330, useAction: UseAction.NONE,
    interactableType: InteractableType.NAVIGATION,
  },
];

export class InteractableFactory implements ItemCategoryFactory {
  readonly categoryName = "interactables";

  registerItems(registry: ItemRegistry): void {
    for (const def of INTERACTABLE_DEFS) {
      registry.register(def);
    }
  }

  getProperties(itemId: number): InteractableProperties | undefined {
    return INTERACTABLE_DEFS.find((d) => d.id === itemId);
  }

  getAllProperties(): InteractableProperties[] {
    return INTERACTABLE_DEFS;
  }
}
```

---

## 4. Durability & Damage System

Tools, weapons, and armor have durability that decreases with use. When durability reaches 0, the item breaks and is removed from the inventory.

```typescript
// /src/engine/ecs/systems/DurabilitySystem.ts

import type { ItemRegistry } from "../../../content/items/ItemRegistry.js";
import type { ToolProperties } from "../../../content/items/ToolFactory.js";
import type { WeaponProperties } from "../../../content/items/WeaponFactory.js";
import type { ArmorProperties } from "../../../content/items/ArmorFactory.js";

/**
 * Manages item durability for tools, weapons, and armor.
 * Durability is stored in the item's metadata field (Int16).
 *
 * Metadata encoding for durable items:
 *   - Positive value: remaining durability
 *   - 0: item is about to break (one more use destroys it)
 *   - Negative: not used (reserved)
 *
 * On item creation (crafting, spawning), durability is initialized
 * to the item's maxDurability value in the metadata field.
 */
export class DurabilitySystem {
  /**
   * Reduce durability by 1. Returns true if the item should be destroyed.
   * O(1) — direct TypedArray access.
   */
  static damage(
    metadata: Int16Array,
    index: number,
    maxDurability: number,
    damageAmount: number = 1,
  ): boolean {
    const current = metadata[index];
    if (current <= 0) return true;

    const remaining = current - damageAmount;
    if (remaining <= 0) {
      metadata[index] = 0;
      return true; // item breaks
    }

    metadata[index] = remaining;
    return false;
  }

  /**
   * Get the current durability percentage for UI rendering.
   * Returns 0.0 (broken) to 1.0 (full).
   */
  static getDurabilityPercent(metadata: number, maxDurability: number): number {
    if (maxDurability <= 0) return 1.0;
    return Math.max(0, Math.min(1, metadata / maxDurability));
  }
}
```

### Durability Events

| Action | Durability Cost | Tool Type |
|---|---|---|
| Breaking a block | 1 | Pickaxe, Axe, Shovel, Hoe |
| Attacking an entity | 2 | Sword, Axe |
| Blocking with shield | 0 | Shield |
| Shearing a sheep | 1 | Shears |
| Striking flint & steel | 1 | Flint and Steel |
| Taking damage (armor) | 1 per 4 damage (rounded) | All armor pieces |
| Using hoe on dirt | 0 | Hoe |
| Fishing | 1 | Fishing Rod |

---

## 5. Tool Harvest Levels & Block Breaking

The breaking speed and drop behavior of a block depend on the tool used.

```
Harvest Level Table:
  Level 0 (Wood):    Stone, dirt, sand, gravel, wood, grass
  Level 1 (Stone):   Coal ore, iron ore, stone, cobblestone, redstone ore
  Level 2 (Iron):    Gold ore, diamond ore, lapis ore, emerald ore
  Level 3 (Diamond): Obsidian
```

```typescript
// /src/engine/systems/BreakingSystem.ts

import type { ItemRegistry } from "../../content/items/ItemRegistry.js";
import type { ToolProperties } from "../../content/items/ToolFactory.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";

export const BLOCK_HARDNESS: Record<number, { hardness: number; harvestLevel: number; requiredTool: boolean }> = {
  // Stone tier
  1:  { hardness: 1.5, harvestLevel: 1, requiredTool: true },  // stone
  4:  { hardness: 2.0, harvestLevel: 0, requiredTool: false }, // cobblestone
  7:  { hardness: -1,  harvestLevel: 0, requiredTool: false }, // bedrock (unbreakable)
  // Ore tier
  14: { hardness: 3.0, harvestLevel: 1, requiredTool: true },  // gold ore
  15: { hardness: 3.0, harvestLevel: 1, requiredTool: true },  // iron ore
  16: { hardness: 3.0, harvestLevel: 0, requiredTool: true },  // coal ore
  56: { hardness: 3.0, harvestLevel: 2, requiredTool: true },  // diamond ore
  // Dirt tier
  2:  { hardness: 0.6, harvestLevel: 0, requiredTool: false }, // grass
  3:  { hardness: 0.5, harvestLevel: 0, requiredTool: false }, // dirt
  12: { hardness: 0.5, harvestLevel: 0, requiredTool: false }, // sand
  // Wood tier
  5:  { hardness: 2.0, harvestLevel: 0, requiredTool: true },  // planks
  17: { hardness: 2.0, harvestLevel: 0, requiredTool: true },  // log
  // Obsidian
  49: { hardness: 50,  harvestLevel: 3, requiredTool: true },  // obsidian
};

/**
 * Calculates block breaking time in seconds.
 *
 * Formula (MC 1.5.2):
 *   base = hardness * 1.5
 *   speed = (isProperTool ? toolSpeed : 1) * (isEffective ? efficiencyBonus : 1)
 *   if canHarvest: time = base / speed
 *   else:          time = base / 5
 *
 * Returns Infinity if unbreakable.
 */
export function calculateBreakTime(
  blockId: number,
  toolProperties?: ToolProperties,
): number {
  const block = BLOCK_HARDNESS[blockId];
  if (!block) return 0.75; // default fallback
  if (block.hardness < 0) return Infinity; // unbreakable

  let speed = 1;
  let canHarvest = true;

  if (toolProperties) {
    const isProperTool = toolProperties.requiredBlocks.includes(blockId);
    const isEffective = toolProperties.effectiveBlocks.includes(blockId);

    if (isProperTool || isEffective) {
      speed = toolProperties.miningSpeed;
    }

    // If the block requires a minimum harvest level
    if (block.requiredTool && toolProperties.harvestLevel < block.harvestLevel) {
      canHarvest = false;
    }
  } else {
    // Hand (no tool)
    if (block.requiredTool) {
      canHarvest = false;
    }
  }

  const baseTime = block.hardness * 1.5;
  if (!canHarvest) return baseTime / 5;

  return baseTime / speed;
}
```

---

## 6. Food & Consumption System

When a player eats food, the `ConsumptionSystem` handles hunger restoration, saturation, and status effects.

```typescript
// /src/engine/ecs/systems/ConsumptionSystem.ts

import type { FoodProperties } from "../../../content/items/FoodFactory.js";

/**
 * Manages player hunger and food consumption.
 *
 * Hunger is stored as a component on the player entity:
 *   - hunger: Int16 (0-20, 20 = full)
 *   - saturation: Float32 (0-20, drains before hunger)
 *   - exhaustion: Float32 (accumulates from actions, drains saturation)
 */
export class ConsumptionSystem {
  /**
   * Apply food effects when consumed.
   *
   * @param food     The food item properties
   * @param hunger   Current hunger level (mutated in-place)
   * @param saturation Current saturation level (mutated in-place)
   * @returns true if the food was successfully consumed
   */
  static consume(
    food: FoodProperties,
    hunger: number,
    saturation: number,
  ): { hunger: number; saturation: number } {
    let newHunger = hunger;
    let newSaturation = saturation;

    // Hunger restoration
    newHunger = Math.min(20, newHunger + food.hungerRestoration);

    // Saturation is added on top, capped at current hunger level
    newSaturation = Math.min(newHunger, newSaturation + food.hungerRestoration * food.saturationModifier);

    return { hunger: newHunger, saturation: newSaturation };
  }

  /**
   * Check if the player can eat the food.
   * Most food requires hunger < 20, unless alwaysEdible.
   */
  static canEat(food: FoodProperties, hunger: number): boolean {
    if (food.alwaysEdible) return true;
    return hunger < 20;
  }
}
```

---

## 7. Default Item Registry Configuration

The composition root that wires all categories together:

```typescript
// /src/content/items/DefaultItems.ts

import { ItemRegistry } from "./ItemRegistry.js";
import { BlockItemFactory } from "./BlockItemFactory.js";
import { ToolFactory } from "./ToolFactory.js";
import { WeaponFactory } from "./WeaponFactory.js";
import { ArmorFactory } from "./ArmorFactory.js";
import { FoodFactory } from "./FoodFactory.js";
import { MaterialFactory } from "./MaterialFactory.js";
import { InteractableFactory } from "./InteractableFactory.js";
import type { BlockRegistry } from "../../world/BlockRegistry.js";

export const createDefaultItemRegistry = (blocks?: BlockRegistry): ItemRegistry => {
  const registry = new ItemRegistry();

  // Order matters: block items depend on BlockRegistry being populated first.
  if (blocks) {
    registry.registerCategory(new BlockItemFactory(blocks));
  }

  registry.registerCategory(new ToolFactory());
  registry.registerCategory(new WeaponFactory());
  registry.registerCategory(new ArmorFactory());
  registry.registerCategory(new FoodFactory());
  registry.registerCategory(new MaterialFactory());
  registry.registerCategory(new InteractableFactory());

  return registry;
};
```

---

## 8. Integration Into Existing Systems

### 8.1 Inventory Component Enhancement

The existing `InventoryComponent` already has `itemMetadata: Int16Array` for each slot, which is used for durability tracking. No structural changes are needed — only the interpretation of metadata changes based on item type.

### 8.2 Crafting Integration

Crafting recipes already use item IDs. With the full item registry, recipes can be validated against known items and can reference tools (which stack to 1) and materials correctly.

### 8.3 Player Interaction Flow (Updated)

When the player right-clicks with an item:

```
PlayerInteractionController.onRightClick()
├── 1. Get held item ID from inventory[selectedHotbarSlot]
├── 2. Look up ItemProperties from ItemRegistry
├── 3. Dispatch based on UseAction:
│   ├── PLACE_BLOCK:      → World.setBlockIdAt() (existing path)
│   ├── EAT:              → ConsumptionSystem.consume() + remove item
│   ├── SWING:            → Play animation, damage entity on hit
│   ├── DRINK:            → Apply potion effects + remove item
│   ├── USE_ON_BLOCK:     → Dispatch to InteractableHandler:
│   │   ├── FLUID_CONTAINER  → Place/pickup fluid
│   │   ├── IGNITER          → Spawn fire block
│   │   ├── GROWTH_AGENT     → Advance crop stage
│   │   ├── TILLER           → Convert dirt/grass to farmland
│   │   └── ... 
│   ├── USE_ON_ENTITY:   → Interact with mob (lead, name tag, shear)
│   └── CHARGE_AND_RELEASE: → Start charge timer (bow, fishing rod)
└── 4. If consumable/durable, decrement count or damage metadata
```

### 8.4 Item Icon Rendering

The existing HUD renders item names as text. For icon rendering, the texture array approach extends naturally:

- Each item gets a dedicated layer in a 2D texture array (or a separate item atlas texture).
- The HUD renders 2D quads with the item's texture layer index from `ItemProperties.iconTextureLayer`.
- The existing `TexturePipeline` is extended to generate item texture layers alongside block texture layers.

```typescript
// Item atlas integration with Renderer
export class ItemAtlas {
  private readonly textureArray: Texture2DArray;

  constructor(gl: WebGL2RenderingContext, registry: ItemRegistry) {
    const itemCount = registry.getAllItems().length;
    this.textureArray = new Texture2DArray(gl, 16, 16, itemCount);
    // Seed layers from item properties
    let layerIndex = 0;
    for (const item of registry.getAllItems()) {
      const pixels = synthesizeItemIcon(item);
      this.textureArray.uploadLayer(layerIndex, pixels, 16, 16);
      layerIndex++;
    }
    this.textureArray.generateMipmaps();
  }
}
```

---

## 9. Item ID Map Summary

| Range          | Category      | Description |
|:---------------|:--------------|:------------|
| 0              | —             | Air / Empty |
| 1-127          | Block Items   | Placeable blocks (matches BlockRegistry) |
| 256-271        | Tools         | Pickaxes (Wood, Stone, Iron, Gold, Diamond) |
| 272-287        | Tools         | Axes (Wood, Stone, Iron, Gold, Diamond) |
| 288-303        | Tools         | Shovels (Wood, Stone, Iron, Gold, Diamond) |
| 304-319        | Tools         | Hoes (Wood, Stone, Iron, Gold, Diamond) |
| 320            | Tools         | Shears |
| 272-277        | Weapons       | Swords (Wood, Stone, Iron, Gold, Diamond) |
| 300            | Weapons       | Bow |
| 301            | Weapons       | Arrow |
| 325-330        | Interactables | Bucket, Water Bucket, Lava Bucket, Flint & Steel, Bone Meal, Compass |
| 340-362        | Materials     | Ingots, gems, dusts, dyes, leather, etc. |
| 512-575        | Armor         | Helmets, Chestplates, Leggings, Boots (× 5 materials) |
| 576-598        | Food          | All edible items |
| 640+           | —             | Reserved for future categories (potions, enchantments, etc.) |

---

## 10. Summary of New Files

```
src/content/items/
├── ItemTypes.ts              — Base enums, interfaces (ItemProperties, ItemCategory, UseAction, etc.)
├── ItemFactory.ts            — ItemCategoryFactory interface
├── ItemRegistry.ts           — Composition root, fast ID lookup
├── DefaultItems.ts           — Default item configuration
├── ToolFactory.ts            — Tool definitions (pickaxe, axe, shovel, hoe, shears)
├── WeaponFactory.ts          — Weapon definitions (swords, bow, arrow)
├── ArmorFactory.ts           — Armor definitions (helmet, chestplate, leggings, boots)
├── FoodFactory.ts            — Food & consumable definitions
├── BlockItemFactory.ts       — Block → Item wrapper
├── MaterialFactory.ts        — Crafting materials (ingots, gems, dyes, etc.)
├── InteractableFactory.ts    — Special-purpose items (bucket, bone meal, etc.)

src/engine/ecs/systems/
├── DurabilitySystem.ts       — Tool/weapon/armor durability tracking
├── ConsumptionSystem.ts      — Food consumption, hunger restoration, effects

src/engine/render/
├── ItemAtlas.ts              — Item texture atlas for HUD rendering
```

---

## 11. Future Extensions

- **Enchantments** — Apply enchantment data to item metadata, affecting tool stats and adding special effects.
- **Potion System** — Brewed potions with duration/amplifier encoded in metadata, `DRINK` use action.
- **Anvil & Repair** — Combine two damaged items of the same type to repair them, consuming one.
- **Item Frames** — Display items on walls as entity attachments.
- **Bundles** — Items that can hold other items (stack limit > 64).
- **Shield** — Blocking mechanic with durability, `USE_ON_ENTITY` interaction.
- **Elytra** — Chestplate-slot item that enables gliding.
- **Smithing Table** — Upgrade diamond tools/armor to netherite.
- **Grindstone** — Remove enchantments, recover some materials.
- **Villager Trading** — Item-for-item trades with defined buy/sell lists.
