# Character Creation System — Abstract Factory & Class-Based Stats

**Version:** 1.0  
**Scope:** Character creation architecture: abstract factory for base characteristics, class definitions with stat allocations, background selection, appearance generation, and the factory pipeline that assembles new characters into the ECS world.  
**Architecture Constraints:** Strict TypeScript, DOD-based ECS with SoA TypedArray storage, zero-copy worker boundaries, data-driven class definitions, no OOP classes for game objects.

---

## 1. System Overview

Every entity that represents a person in the game — player characters, NPCs, companions, enemies — is built from the same character factory pipeline. The pipeline composes characteristics from multiple layers:

```
AbstractCharacterFactory (base traits)
  ├── Species (human, elf, dwarf, beast)
  ├── Class (warrior, mage, rogue, etc.)
  ├── Background (orphan, soldier, scholar, etc.)
  └── Appearance (skin tone, hair, eyes, build)
```

The output is an ECS entity with `CharacterComponent`, `Transform`, `RigidBody`, `Health`, `Inventory`, `Equipment`, and any class-specific components attached.

### Design Goals

1. **Data-driven** — All character definitions are plain objects. Adding a new class or species requires zero new code, only data.
2. **Composable** — Species provides base stat ranges, class provides multipliers and growth, background provides skill bonuses. The factory composes them through superposition.
3. **ECS-native** — Characters are entities with components, not OOP objects. The factory writes directly into SoA TypedArrays.
4. **Deterministic** — Given the same seed and choices, the factory produces the exact same character. Useful for NPC generation.

---

## 2. Abstract Character Factory

### 2.1 Base Characteristic Interface

Every character-producing factory implements this interface:

```typescript
// /src/content/characters/CharacterFactory.ts

import type { EntityManager } from "../../engine/ecs/EntityManager.js";
import type { ComponentStore } from "../../engine/ecs/ComponentStore.js";
import type { TransformDesc } from "../../engine/ecs/components/Transform.js";
import type { HealthDesc } from "../../engine/ecs/components/Health.js";
import type { CharacterDesc } from "../../engine/ecs/components/CharacterComponent.js";
import type { InventoryComponentDesc } from "../../engine/ecs/components/InventoryComponent.js";

/**
 * Result of the character creation pipeline.
 * A fully initialized ECS entity ready for insertion into the world.
 */
export interface CreatedCharacter {
  readonly entityId: number;
  readonly entityIndex: number;
  readonly classId: number;
  readonly speciesId: number;
  readonly backgroundId: number;
  readonly level: number;
  readonly stats: CharacterStats;
}

/**
 * Base characteristics that every character species defines.
 * These are the raw racial/species traits before class modifiers.
 */
export interface SpeciesCharacteristics {
  readonly speciesId: number;
  readonly speciesName: string;
  readonly description: string;
  /** Base stat ranges [min, max] for random NPC generation. */
  readonly statRanges: {
    readonly strength: [number, number];
    readonly dexterity: [number, number];
    readonly intelligence: [number, number];
    readonly vitality: [number, number];
    readonly wisdom: [number, number];
    readonly charisma: [number, number];
    readonly luck: [number, number];
  };
  /** Stat modifiers applied to all members of this species (e.g., Elves +DEX -STR). */
  readonly statModifiers: Partial<Record<string, number>>;
  /** Size multiplier for collision AABB. */
  readonly sizeModifier: number;
  /** Base movement speed. */
  readonly baseSpeed: number;
  /** Available appearance options. */
  readonly appearanceOptions: AppearanceOptions;
  /** Natural abilities (e.g., night vision, fire resistance). */
  readonly innateAbilities: readonly number[];
}

export interface AppearanceOptions {
  readonly skinTones: readonly string[];
  readonly hairColors: readonly string[];
  readonly hairStyles: readonly string[];
  readonly eyeColors: readonly string[];
  readonly buildTypes: readonly string[];
}

export interface CharacterStats {
  readonly strength: number;
  readonly dexterity: number;
  readonly intelligence: number;
  readonly vitality: number;
  readonly wisdom: number;
  readonly charisma: number;
  readonly luck: number;
  readonly maxHp: number;
  readonly maxMana: number;
  readonly maxStamina: number;
}

/**
 * Abstract factory interface for character creation.
 * Every species, class, and background combination flows through this.
 */
export interface CharacterFactory {
  /** The display name of this factory (e.g., "Human", "Elf", "Random NPC"). */
  readonly factoryName: string;

  /**
   * Create a new character entity and populate all its components.
   * This is the main entry point for spawning any character in the world.
   */
  createCharacter(params: CharacterCreationParams): CreatedCharacter;

  /**
   * Roll a random set of creation params suitable for NPC generation.
   * Uses the provided RNG for deterministic output.
   */
  rollRandomParams(rng: () => number): CharacterCreationParams;

  /**
   * Get the species characteristics for a given species ID.
   */
  getSpecies(speciesId: number): SpeciesCharacteristics;

  /**
   * Get all registered species.
   */
  getAllSpecies(): readonly SpeciesCharacteristics[];
}

export interface CharacterCreationParams {
  readonly characterName: string;
  readonly speciesId: number;
  readonly classId: number;
  readonly backgroundId: number;
  readonly level: number;
  readonly appearance: AppearanceSelections;
  /** Optional: override specific stats instead of computing them. */
  readonly statOverrides?: Partial<CharacterStats>;
}

export interface AppearanceSelections {
  readonly skinTone: string;
  readonly hairColor: string;
  readonly hairStyle: string;
  readonly eyeColor: string;
  readonly buildType: string;
}
```

### 2.2 Factory Implementation

The concrete factory composes species, class, and background into a final character:

```typescript
// /src/content/characters/CharacterFactoryImpl.ts

import type { EntityManager } from "../../engine/ecs/EntityManager.js";
import type { ComponentStore } from "../../engine/ecs/ComponentStore.js";
import { CharacterDesc, type CharacterStats } from "../../engine/ecs/components/CharacterComponent.js";
import type { SpeciesRegistry } from "../species/SpeciesRegistry.js";
import type { ClassRegistry, ClassDefinition } from "../classes/ClassRegistry.js";
import type { BackgroundRegistry, BackgroundDefinition } from "../backgrounds/BackgroundRegistry.js";
import { LevelingSystem } from "../../engine/ecs/systems/LevelingSystem.js";
import { computeDerivedStats } from "../stats/StatFormulas.js";

export class CharacterFactoryImpl implements CharacterFactory {
  readonly factoryName = "Default Character Factory";

  constructor(
    private readonly em: EntityManager,
    private readonly characters: ComponentStore<typeof CharacterDesc>,
    private readonly speciesRegistry: SpeciesRegistry,
    private readonly classRegistry: ClassRegistry,
    private readonly backgroundRegistry: BackgroundRegistry,
  ) {}

  createCharacter(params: CharacterCreationParams): CreatedCharacter {
    const entityId = this.em.allocate();
    const entityIndex = EntityManager.indexOf(entityId);

    // 1. Look up component definitions
    const species = this.speciesRegistry.get(params.speciesId);
    const classDef = this.classRegistry.get(params.classId);
    const background = this.backgroundRegistry.get(params.backgroundId);

    // 2. Compute base stats from species ranges, scaled by level
    const baseStats = this.computeBaseStats(species, classDef, params.level);

    // 3. Apply background skill bonuses
    const finalStats = this.applyBackground(baseStats, background, params.level);

    // 4. Apply any explicit overrides
    if (params.statOverrides) {
      this.applyOverrides(finalStats, params.statOverrides);
    }

    // 5. Compute derived stats (HP, Mana, Stamina)
    const derived = computeDerivedStats(finalStats);

    // 6. Write CharacterComponent data
    const row = this.characters.add(entityIndex);
    this.writeCharacterComponent(row, params, species, classDef, finalStats, derived);

    // 7. Initialize Health component
    // (handled by separate system call)

    // 8. Grant starting equipment
    this.grantStartingEquipment(entityIndex, classDef, background);

    // 9. Grant starting skills
    this.grantStartingSkills(entityIndex, classDef, background);

    return {
      entityId,
      entityIndex,
      classId: params.classId,
      speciesId: params.speciesId,
      backgroundId: params.backgroundId,
      level: params.level,
      stats: finalStats,
    };
  }

  private computeBaseStats(
    species: SpeciesCharacteristics,
    classDef: ClassDefinition,
    level: number,
  ): CharacterStats {
    // Species provides base value (midpoint of range)
    const sp = species.statRanges;
    const baseStr = Math.floor((sp.strength[0] + sp.strength[1]) / 2);
    const baseDex = Math.floor((sp.dexterity[0] + sp.dexterity[1]) / 2);
    const baseInt = Math.floor((sp.intelligence[0] + sp.intelligence[1]) / 2);
    const baseVit = Math.floor((sp.vitality[0] + sp.vitality[1]) / 2);
    const baseWis = Math.floor((sp.wisdom[0] + sp.wisdom[1]) / 2);
    const baseCha = Math.floor((sp.charisma[0] + sp.charisma[1]) / 2);
    const baseLuc = Math.floor((sp.luck[0] + sp.luck[1]) / 2);

    // Apply species modifiers
    const mod = species.statModifiers;
    const speciesStr = baseStr + (mod.strength ?? 0);
    const speciesDex = baseDex + (mod.dexterity ?? 0);
    const speciesInt = baseInt + (mod.intelligence ?? 0);
    const speciesVit = baseVit + (mod.vitality ?? 0);
    const speciesWis = baseWis + (mod.wisdom ?? 0);
    const speciesCha = baseCha + (mod.charisma ?? 0);
    const speciesLuc = baseLuc + (mod.luck ?? 0);

    // Class provides base stats + per-level growth
    const lvl = level - 1;
    const cls = classDef.baseStats;
    const growth = classDef.statGrowthPerLevel;
    const classStr = cls.strength + growth.strength * lvl;
    const classDex = cls.dexterity + growth.dexterity * lvl;
    const classInt = cls.intelligence + growth.intelligence * lvl;
    const classVit = cls.vitality + growth.vitality * lvl;
    const classWis = cls.wisdom + growth.wisdom * lvl;
    const classCha = cls.charisma + growth.charisma * lvl;

    // Final stat = species base + class contribution (weighted average)
    // Species contributes 40%, class contributes 60% to make class identity dominant
    return {
      strength: Math.floor(speciesStr * 0.4 + classStr * 0.6),
      dexterity: Math.floor(speciesDex * 0.4 + classDex * 0.6),
      intelligence: Math.floor(speciesInt * 0.4 + classInt * 0.6),
      vitality: Math.floor(speciesVit * 0.4 + classVit * 0.6),
      wisdom: Math.floor(speciesWis * 0.4 + classWis * 0.6),
      charisma: Math.floor(speciesCha * 0.4 + classCha * 0.6),
      luck: Math.floor(speciesLuc * 0.4 + classCha * 0.1), // luck growth tied loosely to class
      maxHp: 0, // computed by computeDerivedStats
      maxMana: 0,
      maxStamina: 0,
    };
  }

  private applyBackground(
    stats: CharacterStats,
    background: BackgroundDefinition,
    level: number,
  ): CharacterStats {
    // Backgrounds provide small flat bonuses (2-4 points) to 2-3 stats
    return {
      ...stats,
      strength: stats.strength + (background.statBonuses.strength ?? 0),
      dexterity: stats.dexterity + (background.statBonuses.dexterity ?? 0),
      intelligence: stats.intelligence + (background.statBonuses.intelligence ?? 0),
      vitality: stats.vitality + (background.statBonuses.vitality ?? 0),
      wisdom: stats.wisdom + (background.statBonuses.wisdom ?? 0),
      charisma: stats.charisma + (background.statBonuses.charisma ?? 0),
      luck: stats.luck + (background.statBonuses.luck ?? 0),
    };
  }

  private applyOverrides(stats: CharacterStats, overrides: Partial<CharacterStats>): void {
    if (overrides.strength !== undefined) stats.strength = overrides.strength;
    if (overrides.dexterity !== undefined) stats.dexterity = overrides.dexterity;
    if (overrides.intelligence !== undefined) stats.intelligence = overrides.intelligence;
    if (overrides.vitality !== undefined) stats.vitality = overrides.vitality;
    if (overrides.wisdom !== undefined) stats.wisdom = overrides.wisdom;
    if (overrides.charisma !== undefined) stats.charisma = overrides.charisma;
    if (overrides.luck !== undefined) stats.luck = overrides.luck;
  }

  private writeCharacterComponent(
    row: number,
    params: CharacterCreationParams,
    species: SpeciesCharacteristics,
    classDef: ClassDefinition,
    stats: CharacterStats,
    derived: DerivedStats,
  ): void {
    const data = this.characters.data;

    // Pack character name into Uint32Array (8 chars max)
    const nameBytes = new TextEncoder().encode(params.characterName.padEnd(8, "\0").slice(0, 8));
    for (let i = 0; i < 8; i++) {
      data.characterName[row * 8 + i] = nameBytes[i] ?? 0;
    }

    data.level[row] = params.level;
    data.experience[row] = LevelingSystem.xpForLevel(params.level);
    data.classId[row] = params.classId;
    data.speciesId[row] = params.speciesId;
    data.backgroundId[row] = params.backgroundId;

    data.baseStrength[row] = stats.strength;
    data.baseDexterity[row] = stats.dexterity;
    data.baseIntelligence[row] = stats.intelligence;
    data.baseVitality[row] = stats.vitality;
    data.baseWisdom[row] = stats.wisdom;
    data.baseCharisma[row] = stats.charisma;
    data.baseLuck[row] = stats.luck;

    data.mana[row] = derived.maxMana;
    data.maxMana[row] = derived.maxMana;
    data.stamina[row] = derived.maxStamina;
    data.maxStamina[row] = derived.maxStamina;
    data.manaRegen[row] = derived.manaRegen;

    data.factionId[row] = 0;
    data.perkPoints[row] = 0;
    data.skillPoints[row] = Math.floor(params.level / 2);
  }

  private grantStartingEquipment(entityIndex: number, classDef: ClassDefinition, background: BackgroundDefinition): void {
    // Equipment is placed into the character's inventory
    // Handled by InventoryComponent writes
  }

  private grantStartingSkills(entityIndex: number, classDef: ClassDefinition, background: BackgroundDefinition): void {
    // Skills are written into SkillComponent
  }

  rollRandomParams(rng: () => number): CharacterCreationParams {
    const speciesList = this.speciesRegistry.getAll();
    const classList = this.classRegistry.getAll();
    const bgList = this.backgroundRegistry.getAll();

    const species = speciesList[Math.floor(rng() * speciesList.length)];
    const classDef = classList[Math.floor(rng() * classList.length)];
    const background = bgList[Math.floor(rng() * bgList.length)];

    const level = Math.floor(rng() * 30) + 1;

    return {
      characterName: this.generateName(rng),
      speciesId: species.speciesId,
      classId: classDef.id,
      backgroundId: background.id,
      level,
      appearance: this.randomAppearance(species, rng),
    };
  }

  private generateName(rng: () => number): string {
    const prefixes = ["Ald", "Bra", "Cor", "Der", "El", "Far", "Gar", "Hel", "Ir", "Jor",
      "Kal", "Lor", "Mar", "Nor", "Or", "Per", "Qui", "Ral", "Sor", "Tor"];
    const middles = ["a", "e", "i", "o", "u", "an", "en", "in", "on", "un",
      "ar", "er", "ir", "or", "ur", "al", "el", "il", "ol", "ul"];
    const suffixes = ["dor", "mir", "thar", "wind", "stone", "born", "wick", "ford", "lund", "mere",
      "ian", "ius", "a", "is", "us", "on", "as", "os", "en", "ar"];
    const p = prefixes[Math.floor(rng() * prefixes.length)];
    const m = middles[Math.floor(rng() * middles.length)];
    const s = suffixes[Math.floor(rng() * suffixes.length)];
    return p + m + s;
  }

  private randomAppearance(species: SpeciesCharacteristics, rng: () => number): AppearanceSelections {
    const opts = species.appearanceOptions;
    return {
      skinTone: opts.skinTones[Math.floor(rng() * opts.skinTones.length)],
      hairColor: opts.hairColors[Math.floor(rng() * opts.hairColors.length)],
      hairStyle: opts.hairStyles[Math.floor(rng() * opts.hairStyles.length)],
      eyeColor: opts.eyeColors[Math.floor(rng() * opts.eyeColors.length)],
      buildType: opts.buildTypes[Math.floor(rng() * opts.buildTypes.length)],
    };
  }
}
```

---

## 3. Species Definitions

Species define the biological baseline of a character — their natural stat tendencies, size, speed, and innate abilities.

```typescript
// /src/content/species/SpeciesRegistry.ts

import type { SpeciesCharacteristics } from "../characters/CharacterFactory.js";

/**
 * Species define the biological foundation of a character.
 * Every character (player or NPC) belongs to exactly one species.
 */
export const enum SpeciesId {
  HUMAN,
  ELF,
  DWARF,
  ORC,
  HALFLING,
  BEASTKIN,
  AVIAN,
  AQUATIC,
  UNDEAD,
  DEMON,
}

const SPECIES_DEFINITIONS: SpeciesCharacteristics[] = [
  {
    speciesId: SpeciesId.HUMAN,
    speciesName: "Human",
    description: "Versatile and adaptable. Humans excel at no single stat but have no weakness.",
    statRanges: {
      strength: [6, 12], dexterity: [6, 12], intelligence: [6, 12],
      vitality: [6, 12], wisdom: [6, 12], charisma: [6, 12], luck: [5, 10],
    },
    statModifiers: {}, // no modifiers — baseline species
    sizeModifier: 1.0,
    baseSpeed: 4.3,
    appearanceOptions: {
      skinTones: ["fair", "tan", "olive", "brown", "dark", "pale"],
      hairColors: ["blonde", "brown", "black", "red", "auburn", "grey", "white"],
      hairStyles: ["short", "long", "bald", "ponytail", "braided", "curly", "spiked"],
      eyeColors: ["blue", "green", "brown", "grey", "hazel", "amber"],
      buildTypes: ["slim", "athletic", "stocky", "lanky", "average"],
    },
    innateAbilities: [], // humans get a bonus perk point instead (handled separately)
  },
  {
    speciesId: SpeciesId.ELF,
    speciesName: "Elf",
    description: "Graceful and long-lived. Elves have heightened dexterity and wisdom but lower vitality.",
    statRanges: {
      strength: [4, 8], dexterity: [10, 16], intelligence: [8, 14],
      vitality: [4, 8], wisdom: [10, 16], charisma: [8, 12], luck: [5, 10],
    },
    statModifiers: { dexterity: 2, wisdom: 2, strength: -2, vitality: -2 },
    sizeModifier: 0.95,
    baseSpeed: 4.6,
    appearanceOptions: {
      skinTones: ["fair", "pale", "ivory", "rose"],
      hairColors: ["blonde", "silver", "white", "copper", "golden", "black"],
      hairStyles: ["long", "flowing", "braided", "elaborate", "simple", "short"],
      eyeColors: ["blue", "green", "silver", "amber", "violet", "golden"],
      buildTypes: ["slim", "wiry", "lithe", "graceful", "delicate"],
    },
    innateAbilities: [100], // Night Vision
  },
  {
    speciesId: SpeciesId.DWARF,
    speciesName: "Dwarf",
    description: "Sturdy and resilient. Dwarves have high strength and vitality but lower speed and charisma.",
    statRanges: {
      strength: [10, 16], dexterity: [4, 8], intelligence: [6, 10],
      vitality: [12, 18], wisdom: [6, 10], charisma: [4, 8], luck: [5, 10],
    },
    statModifiers: { strength: 2, vitality: 3, dexterity: -2, charisma: -2, speed: -0.3 },
    sizeModifier: 0.8,
    baseSpeed: 4.0,
    appearanceOptions: {
      skinTones: ["fair", "tan", "ruddy", "weathered", "pale"],
      hairColors: ["brown", "black", "red", "auburn", "grey", "white", "copper"],
      hairStyles: ["short", "bald", "long", "braided", "bearded", "wild"],
      eyeColors: ["brown", "grey", "green", "blue", "amber"],
      buildTypes: ["stocky", "stout", "broad", "barrel-chested", "solid"],
    },
    innateAbilities: [101], // Dark Vision
  },
  {
    speciesId: SpeciesId.ORC,
    speciesName: "Orc",
    description: "Powerful and intimidating. Orcs have immense strength but lower intelligence and wisdom.",
    statRanges: {
      strength: [14, 20], dexterity: [6, 10], intelligence: [3, 7],
      vitality: [12, 18], wisdom: [3, 7], charisma: [3, 7], luck: [3, 8],
    },
    statModifiers: { strength: 4, vitality: 2, intelligence: -3, wisdom: -3, charisma: -2 },
    sizeModifier: 1.15,
    baseSpeed: 4.5,
    appearanceOptions: {
      skinTones: ["green", "dark green", "grey-green", "olive", "moss"],
      hairColors: ["black", "brown", "dark red", "grey", "white"],
      hairStyles: ["short", "bald", "mohawk", "braided", "wild", "spiked"],
      eyeColors: ["red", "yellow", "orange", "brown", "black"],
      buildTypes: ["massive", "bulky", "hulking", "muscular", "imposing"],
    },
    innateAbilities: [102], // Intimidating Presence
  },
  {
    speciesId: SpeciesId.HALFLING,
    speciesName: "Halfling",
    description: "Small and lucky. Halflings excel at dexterity and charisma but lack strength.",
    statRanges: {
      strength: [3, 7], dexterity: [10, 16], intelligence: [6, 12],
      vitality: [6, 12], wisdom: [8, 14], charisma: [10, 16], luck: [10, 18],
    },
    statModifiers: { dexterity: 2, charisma: 2, luck: 3, strength: -3 },
    sizeModifier: 0.65,
    baseSpeed: 4.2,
    appearanceOptions: {
      skinTones: ["fair", "tan", "pale", "rosy", "warm"],
      hairColors: ["brown", "blonde", "red", "auburn", "chestnut", "curly"],
      hairStyles: ["curly", "short", "long", "messy", "braided", "bald"],
      eyeColors: ["brown", "green", "blue", "hazel", "grey"],
      buildTypes: ["plump", "slim", "stout", "small", "round"],
    },
    innateAbilities: [103], // Lucky
  },
  {
    speciesId: SpeciesId.BEASTKIN,
    speciesName: "Beastkin",
    description: "Part human, part beast. Beastkin have heightened senses and physical prowess.",
    statRanges: {
      strength: [8, 16], dexterity: [10, 16], intelligence: [4, 10],
      vitality: [8, 14], wisdom: [6, 10], charisma: [4, 10], luck: [4, 8],
    },
    statModifiers: { strength: 1, dexterity: 2, intelligence: -2 },
    sizeModifier: 1.05,
    baseSpeed: 4.8,
    appearanceOptions: {
      skinTones: ["furred", "scaled", "spotted", "striped", "tawny", "pale"],
      hairColors: ["brown", "black", "white", "orange", "grey", "golden"],
      hairStyles: ["mane", "short", "tufted", "spiked", "sleek", "wild"],
      eyeColors: ["golden", "green", "yellow", "blue", "red", "amber"],
      buildTypes: ["lean", "powerful", "agile", "feral", "muscular"],
    },
    innateAbilities: [104], // Keen Senses
  },
];

export class SpeciesRegistry {
  private readonly species = new Map<number, SpeciesCharacteristics>();

  constructor() {
    for (const def of SPECIES_DEFINITIONS) {
      this.species.set(def.speciesId, def);
    }
  }

  get(speciesId: number): SpeciesCharacteristics {
    const def = this.species.get(speciesId);
    if (!def) throw new Error(`Unknown species ${speciesId}`);
    return def;
  }

  getAll(): readonly SpeciesCharacteristics[] {
    return SPECIES_DEFINITIONS;
  }
}
```

---

## 4. Background Definitions

Backgrounds represent a character's life before becoming an adventurer. They provide small stat bonuses, additional starting skills, and equipment.

```typescript
// /src/content/backgrounds/BackgroundRegistry.ts

import type { CharacterStats } from "../characters/CharacterFactory.js";

export const enum BackgroundId {
  ORPHAN,
  SOLDIER,
  SCHOLAR,
  MERCHANT,
  HERMIT,
  CRIMINAL,
  NOBLE,
  PEASANT,
  ARTISAN,
  HUNTER,
  SAGE,
  OUTLAW,
  CLERIC_NOVICE,
  BARD_TRAVELER,
  SCOUT,
}

export interface BackgroundDefinition {
  readonly id: BackgroundId;
  readonly name: string;
  readonly description: string;
  /** Small flat stat bonuses (2-4 points to 2-3 stats). */
  readonly statBonuses: Partial<{
    strength: number;
    dexterity: number;
    intelligence: number;
    vitality: number;
    wisdom: number;
    charisma: number;
    luck: number;
  }>;
  /** Additional starting skills granted by this background. */
  readonly bonusSkills: readonly number[];
  /** Starting equipment items granted. */
  readonly bonusEquipment: readonly number[];
  /** Bonus starting gold (copper pieces). */
  readonly startingGold: number;
  /** A short description of what the character did. */
  readonly flavorText: string;
}

const BACKGROUND_DEFINITIONS: BackgroundDefinition[] = [
  {
    id: BackgroundId.ORPHAN,
    name: "Orphan",
    description: "Survived alone on the streets from a young age.",
    statBonuses: { dexterity: 2, luck: 2 },
    bonusSkills: [21], // Stealth
    bonusEquipment: [280, 280], // 2 sticks (makeshift weapons)
    startingGold: 5,
    flavorText: "You learned to fend for yourself in the alleys and gutters. Nothing was given; everything was taken.",
  },
  {
    id: BackgroundId.SOLDIER,
    name: "Soldier",
    description: "Served in a military company or town guard.",
    statBonuses: { strength: 2, vitality: 2 },
    bonusSkills: [1], // Slash
    bonusEquipment: [272, 1], // Sword, stone (whetstone)
    startingGold: 20,
    flavorText: "You marched, fought, and bled for a cause — whether you believed in it or not. Discipline is second nature.",
  },
  {
    id: BackgroundId.SCHOLAR,
    name: "Scholar",
    description: "Studied in a library, academy, or under a private tutor.",
    statBonuses: { intelligence: 3, wisdom: 1 },
    bonusSkills: [10], // Magic Bolt (basic arcane knowledge)
    bonusEquipment: [300, 700], // Staff (focus), water vial
    startingGold: 15,
    flavorText: "Knowledge was your weapon and shield. The answers to the world's mysteries lie written in forgotten tongues.",
  },
  {
    id: BackgroundId.MERCHANT,
    name: "Merchant",
    description: "Ran a shop or traded goods between towns.",
    statBonuses: { charisma: 3, luck: 1 },
    bonusSkills: [64], // Persuasion
    bonusEquipment: [900, 900, 900], // 3 gold coins
    startingGold: 50,
    flavorText: "You learned the art of the deal — when to speak, when to listen, and when to walk away.",
  },
  {
    id: BackgroundId.HERMIT,
    name: "Hermit",
    description: "Lived in isolation, surviving off the land.",
    statBonuses: { wisdom: 2, vitality: 2 },
    bonusSkills: [42], // Tracking
    bonusEquipment: [288, 150], // Knife (shovel reimagined), dried herbs
    startingGold: 3,
    flavorText: "Silence was your teacher. The wilds shaped you into someone who needs nothing and fears little.",
  },
  {
    id: BackgroundId.CRIMINAL,
    name: "Criminal",
    description: "Operated outside the law as a thief, smuggler, or bandit.",
    statBonuses: { dexterity: 2, luck: 2 },
    bonusSkills: [21, 22], // Stealth, Lockpicking
    bonusEquipment: [320, 355], // Lockpicks (shears reimagined), flint
    startingGold: 25,
    flavorText: "The law is just a set of rules for those who can't make their own. You took what you needed.",
  },
  {
    id: BackgroundId.NOBLE,
    name: "Noble",
    description: "Born into wealth and privilege.",
    statBonuses: { charisma: 3, intelligence: 1 },
    bonusSkills: [64, 65], // Persuasion, Etiquette
    bonusEquipment: [341, 341, 330], // Gold ingots, compass
    startingGold: 100,
    flavorText: "You never wanted for anything — except purpose. The world beyond your estate beckons.",
  },
  {
    id: BackgroundId.PEASANT,
    name: "Peasant",
    description: "Worked the land as a farmer or laborer.",
    statBonuses: { vitality: 3, strength: 1 },
    bonusSkills: [80], // Farming
    bonusEquipment: [304, 115, 115, 115], // Hoe, wheat seeds x3
    startingGold: 8,
    flavorText: "You know the weight of a day's labor. Your hands are calloused and your back is strong.",
  },
  {
    id: BackgroundId.ARTISAN,
    name: "Artisan",
    description: "Apprenticed as a crafter of tools, armor, or art.",
    statBonuses: { dexterity: 2, intelligence: 2 },
    bonusSkills: [70], // Crafting
    bonusEquipment: [340, 5, 5], // Iron ingot, planks x2
    startingGold: 30,
    flavorText: "You shaped raw materials into works of function and beauty. Every tool tells a story.",
  },
  {
    id: BackgroundId.HUNTER,
    name: "Hunter",
    description: "Tracked and harvested game in the wilderness.",
    statBonuses: { dexterity: 2, wisdom: 1 },
    bonusSkills: [40, 42], // Power Shot, Tracking
    bonusEquipment: [288, 301], // Bow (shovel reimagined), arrows
    startingGold: 12,
    flavorText: "The hunt teaches patience, precision, and respect for the cycle of life and death.",
  },
  {
    id: BackgroundId.SAGE,
    name: "Sage",
    description: "Spent decades studying ancient texts and natural philosophy.",
    statBonuses: { intelligence: 2, wisdom: 2 },
    bonusSkills: [11, 12], // Mana Shield, Arcane Bolt
    bonusEquipment: [300, 359, 359], // Staff, rare herbs x2
    startingGold: 20,
    flavorText: "Ancient knowledge pulses in your mind. The veil between worlds grows thin when you speak the old words.",
  },
  {
    id: BackgroundId.OUTLAW,
    name: "Outlaw",
    description: "A wanted fugitive living beyond the reach of the law.",
    statBonuses: { strength: 2, dexterity: 1, luck: 1 },
    bonusSkills: [20, 21], // Backstab, Stealth
    bonusEquipment: [256, 355], // Dagger (sword reimagined), flint
    startingGold: 15,
    flavorText: "The price on your head grows daily. Every stranger is a potential bounty hunter.",
  },
  {
    id: BackgroundId.CLERIC_NOVICE,
    name: "Novice Cleric",
    description: "Trained in a temple or monastery before taking up the adventuring life.",
    statBonuses: { wisdom: 3, charisma: 1 },
    bonusSkills: [30, 31], // Heal, Smite
    bonusEquipment: [300, 700, 701], // Staff, holy symbol (water vial reimagined), lifeleaf
    startingGold: 10,
    flavorText: "Faith carried you through the darkest hours. Now you carry that faith to those in need.",
  },
  {
    id: BackgroundId.BARD_TRAVELER,
    name: "Traveling Bard",
    description: "Wandered from town to town, trading songs for supper.",
    statBonuses: { charisma: 3, dexterity: 1 },
    bonusSkills: [60, 61], // Inspiring Melody, Dissonance
    bonusEquipment: [300, 354], // Instrument (bow reimagined), feather (for writing)
    startingGold: 20,
    flavorText: "Every tavern has a story, and every story has a price. You've collected both in abundance.",
  },
  {
    id: BackgroundId.SCOUT,
    name: "Scout",
    description: "Explored uncharted territories as a pathfinder for expeditions.",
    statBonuses: { dexterity: 2, wisdom: 2 },
    bonusSkills: [41, 80], // Tracking, Survival
    bonusEquipment: [288, 330, 150], // Bow, compass, dried meat
    startingGold: 18,
    flavorText: "The map ends where your footsteps begin. You've seen places no living soul can name.",
  },
];

export class BackgroundRegistry {
  private readonly backgrounds = new Map<number, BackgroundDefinition>();

  constructor() {
    for (const def of BACKGROUND_DEFINITIONS) {
      this.backgrounds.set(def.id, def);
    }
  }

  get(backgroundId: number): BackgroundDefinition {
    const def = this.backgrounds.get(backgroundId);
    if (!def) throw new Error(`Unknown background ${backgroundId}`);
    return def;
  }

  getAll(): readonly BackgroundDefinition[] {
    return BACKGROUND_DEFINITIONS;
  }
}
```

---

## 5. Player Character Creation Flow

When a player creates a new character, the UI guides them through a multi-step process:

```
Player Character Creation Flow:
┌─────────────────────────────────────────────────────────────┐
│ Step 1: Choose Name                                         │
│   [_________________________]  (max 16 chars)              │
├─────────────────────────────────────────────────────────────┤
│ Step 2: Choose Species                                      │
│   ○ Human  (Versatile, no weaknesses)                      │
│   ○ Elf    (Dexterous, wise, fragile)                      │
│   ○ Dwarf  (Strong, sturdy, slow)                          │
│   ○ Orc    (Powerful, tough, brutish)                      │
│   ○ Halfling (Lucky, nimble, small)                        │
│   ○ Beastkin (Feral, keen senses)                          │
├─────────────────────────────────────────────────────────────┤
│ Step 3: Choose Class                                        │
│   [Warrior] [Mage] [Rogue] [Cleric] [Ranger]               │
│   [Paladin] [Necromancer] [Bard]                            │
│   (Each shows preview of base stats)                       │
├─────────────────────────────────────────────────────────────┤
│ Step 4: Choose Background                                   │
│   [Orphan] [Soldier] [Scholar] [Merchant] [Hermit]         │
│   [Criminal] [Noble] [Peasant] [Artisan] [Hunter]          │
│   [Sage] [Outlaw] [Novice Cleric] [Bard Traveler] [Scout]  │
├─────────────────────────────────────────────────────────────┤
│ Step 5: Customize Appearance                                │
│   Skin: [____] Hair: [____] Style: [____]                  │
│   Eyes: [____] Build: [____]                               │
├─────────────────────────────────────────────────────────────┤
│ Step 6: Confirm & Create                                    │
│   [Character Preview Panel]                                 │
│   [Stats Preview] [Skills Preview] [Equipment Preview]     │
│                                                             │
│   [  CREATE CHARACTER  ]  [  REROLL  ]                     │
└─────────────────────────────────────────────────────────────┘
```

```typescript
// /src/ui/character/CharacterCreationUI.ts

/**
 * Manages the character creation screen.
 * Collects user selections and calls CharacterFactoryImpl.createCharacter()
 * when the player confirms.
 */
export class CharacterCreationUI {
  private step = 1;
  private params: Partial<CharacterCreationParams> = {};

  constructor(
    private readonly factory: CharacterFactory,
    private readonly onComplete: (character: CreatedCharacter) => void,
  ) {}

  advanceStep(selections: Partial<CharacterCreationParams>): void {
    Object.assign(this.params, selections);
    this.step++;
    this.render();
  }

  confirm(): void {
    if (!this.isComplete()) return;
    const character = this.factory.createCharacter(this.params as CharacterCreationParams);
    this.onComplete(character);
  }

  rerollStats(): void {
    // Keep species/class/background, re-roll stat distribution
    // using a point-buy or random allocation within species ranges
  }

  private isComplete(): boolean {
    return Boolean(
      this.params.characterName &&
      this.params.speciesId !== undefined &&
      this.params.classId !== undefined &&
      this.params.backgroundId !== undefined &&
      this.params.appearance
    );
  }

  private render(): void {
    // Render the current step's UI panel
  }
}
```

---

## 6. NPC Character Generation

NPCs use the same factory but with `rollRandomParams` for procedural generation:

```typescript
// /src/engine/ecs/systems/NpcSpawnSystem.ts

/**
 * Spawns NPC characters into the world using the character factory.
 *
 * NPCs are generated with:
 *   - Random species weighted by region biome
 *   - Random class weighted by building type (blacksmith → warrior, temple → cleric)
 *   - Level scaled to region difficulty
 *   - Name generated from species-appropriate naming tables
 *   - Equipment appropriate to their class and level
 *   - Daily schedule assigned based on their role
 */
export class NpcSpawnSystem {
  constructor(
    private readonly factory: CharacterFactory,
    private readonly rng: () => number,
  ) {}

  spawnTownNpcs(townCenterX: number, townCenterZ: number, townSize: number): void {
    const npcCount = 5 + townSize * 3;
    for (let i = 0; i < npcCount; i++) {
      const params = this.factory.rollRandomParams(this.rng);
      // Adjust params based on town context
      params.level = Math.max(1, Math.floor(this.rng() * 5) + 1);
      const character = this.factory.createCharacter(params);
      // Place in world at a random position within the town
      // Assign a schedule based on character class/background
      // Register as a town NPC for quest giver / trading purposes
    }
  }

  spawnDungeonBoss(difficulty: number): CreatedCharacter {
    const params = this.factory.rollRandomParams(this.rng);
    params.level = 5 + difficulty * 3;
    // Force aggressive class choices
    params.classId = [0, 4, 6][Math.floor(this.rng() * 3)]; // Warrior, Paladin, Necromancer
    return this.factory.createCharacter(params);
  }
}
```

---

## 7. Classes (Extended from RPG Vision)

The classes defined in `rpg-vision.md` are integrated here with the addition of the `luck` stat and species/background composition. The `ClassDefinition` gains a luck growth component:

```typescript
// Extension to ClassDefinition from rpg-vision.md
export interface ClassDefinition {
  // ... existing fields ...
  readonly baseStats: {
    readonly strength: number;
    readonly dexterity: number;
    readonly intelligence: number;
    readonly vitality: number;
    readonly wisdom: number;
    readonly charisma: number;
    readonly luck: number;           // NEW
  };
  readonly statGrowthPerLevel: {
    readonly strength: number;
    readonly dexterity: number;
    readonly intelligence: number;
    readonly vitality: number;
    readonly wisdom: number;
    readonly charisma: number;
    readonly luck: number;           // NEW
  };
}

// Luck growth per class:
// Warrior: 0.5, Mage: 0.5, Rogue: 1.5, Cleric: 1.0
// Ranger: 1.0, Paladin: 0.5, Necromancer: 0.5, Bard: 2.0
```

---

## 8. ECS Component Update

The `CharacterComponent` is extended with the new fields referenced by the factory:

```typescript
// /src/engine/ecs/components/CharacterComponent.ts — Extended

export const CharacterDesc = {
  // Identity
  characterName: { type: Uint32Array, length: 8 },
  level:         { type: Uint16Array, length: 1 },
  experience:    { type: Uint32Array, length: 1 },
  classId:       { type: Uint8Array,  length: 1 },
  speciesId:     { type: Uint8Array,  length: 1 },   // NEW
  backgroundId:  { type: Uint8Array,  length: 1 },   // NEW

  // Core Stats (base values before modifiers)
  baseStrength:     { type: Uint8Array, length: 1 },
  baseDexterity:    { type: Uint8Array, length: 1 },
  baseIntelligence: { type: Uint8Array, length: 1 },
  baseVitality:     { type: Uint8Array, length: 1 },
  baseWisdom:       { type: Uint8Array, length: 1 },
  baseCharisma:     { type: Uint8Array, length: 1 },
  baseLuck:         { type: Uint8Array, length: 1 }, // NEW

  // Derived resources
  mana:          { type: Float32Array, length: 1 },
  maxMana:       { type: Float32Array, length: 1 },
  stamina:       { type: Float32Array, length: 1 },
  maxStamina:    { type: Float32Array, length: 1 },
  manaRegen:     { type: Float32Array, length: 1 },

  // Faction & reputation
  factionId:     { type: Uint8Array,  length: 1 },
  reputation:    { type: Int32Array,  length: 8 },

  // Progression
  perkPoints:    { type: Uint8Array,  length: 1 },
  skillPoints:   { type: Uint8Array,  length: 1 },
} as const satisfies ComponentDesc;
```

---

## 9. Summary of New Files

```
src/content/
├── characters/
│   ├── CharacterFactory.ts        — Abstract factory interface, CharacterStats, CreatedCharacter
│   └── CharacterFactoryImpl.ts    — Concrete factory: composes species + class + background
├── species/
│   └── SpeciesRegistry.ts         — 6 species (Human, Elf, Dwarf, Orc, Halfling, Beastkin)
├── backgrounds/
│   └── BackgroundRegistry.ts      — 15 backgrounds with stat bonuses, skills, equipment
├── stats/
│   └── StatFormulas.ts            — Derived stat computation formulas (see stat-system.md)

src/engine/ecs/components/
└── CharacterComponent.ts          — Extended with speciesId, backgroundId, baseLuck

src/ui/character/
└── CharacterCreationUI.ts         — Multi-step character creation screen

src/engine/ecs/systems/
└── NpcSpawnSystem.ts              — Procedural NPC generation using the factory
```

---

## 10. Factory Composition Diagram

```
CharacterFactoryImpl.createCharacter()
│
├── SpeciesRegistry.get(speciesId)
│   └── SpeciesCharacteristics
│       ├── statRanges [min, max]     → base stat midpoints
│       ├── statModifiers              → species biases (+2 DEX, -2 STR, etc.)
│       ├── sizeModifier               → collision AABB scaling
│       ├── baseSpeed                  → movement speed
│       ├── appearanceOptions          → visual generation
│       └── innateAbilities            → racial powers
│
├── ClassRegistry.get(classId)
│   └── ClassDefinition
│       ├── baseStats                  → class foundation
│       ├── statGrowthPerLevel         → per-level progression
│       ├── startingSkills
│       ├── startingEquipment
│       ├── perkTreeIds
│       └── signatureAbilityId
│
├── BackgroundRegistry.get(backgroundId)
│   └── BackgroundDefinition
│       ├── statBonuses                → small flat bonuses
│       ├── bonusSkills
│       ├── bonusEquipment
│       └── startingGold
│
├── computeDerivedStats(stats)
│   └── DerivedStats
│       ├── maxHp, maxMana, maxStamina
│       ├── hpRegen, manaRegen, staminaRegen
│       ├── meleeDamage, rangedDamage, magicDamage
│       ├── attackSpeed, dodgeChance
│       ├── criticalChance, criticalDamage
│       ├── spellCostReduction, carryWeight
│       └── luckMultiplier
│
├── writeCharacterComponent(entity, stats, derived)
│   └── Populates CharacterDesc SoA TypedArrays
│
├── grantStartingEquipment(entity, classDef, background)
│   └── Writes to InventoryComponent
│
└── grantStartingSkills(entity, classDef, background)
    └── Writes to SkillComponent
```
