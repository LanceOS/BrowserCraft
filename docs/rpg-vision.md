# RPG Terrain Game — Design Vision



**Version:** 1.0**Scope:** Re-imagining the terrain engine as a standalone RPG, breaking away from Minecraft conventions. Characters, classes, stats, magic, quests, trading, factions, and progression systems.**Architecture Constraints:** Strict TypeScript, DOD-based ECS with SoA TypedArray storage, zero-copy worker boundaries via `SharedArrayBuffer`/`Atomics`, WebGL2, no OOP classes for game objects.


---

## 1. Vision Statement

This is not a Minecraft clone. It is a terrain-based action RPG with a living world — procedurally generated continents with towns, dungeons, ruins, and wilderness. The player creates a character, chooses a class, levels up through combat and quests, learns magic, trades with NPCs, builds alliances with factions, and shapes the world.

The underlying engine (chunked terrain world, ECS, WebGL2 renderer, SurfaceNets meshing, shared-memory workers) provides the technical foundation. This document defines the *game* layer built on top of it.

### Design Pillars


1. **Character-Driven Progression** — Classes, stats, skills, and perk trees define who you are, not what you carry.
2. **Living World** — NPCs with schedules, faction territories, dynamic events, and quests that change the world state.
3. **Magic as a System** — Spells are crafted, learned, and combined. Magic has school affinities, mana costs, and casting mechanics.
4. **Meaningful Building** — Construction serves a purpose: workshops for crafting, altars for magic, fortifications for defense, homes for NPCs.
5. **Procedural Depth** — Towns have histories, dungeons have bosses, loot scales with region difficulty.


---

## 2. Character System

### 2.1 Character Creation

Players begin by creating a character with a class, background, and starting stats. The character is an ECS entity with a `CharacterComponent` attached alongside `Transform`, `RigidBody`, `Health`, `Inventory`, and `PlayerComponent`.

```typescript
// /src/engine/ecs/components/CharacterComponent.ts

import type { ComponentDesc } from "../ComponentStore.js";

/**
 * Core character definition — the RPG layer on top of the player entity.
 * Stored as a single SoA component attached to the player entity.
 */
export const CharacterDesc = {
  // Identity
  characterName: { type: Uint32Array, length: 8 },  // 8 packed chars (32 bytes for name)
  level:         { type: Uint16Array, length: 1 },   // 1-999
  experience:    { type: Uint32Array, length: 1 },   // total XP
  classId:       { type: Uint8Array,  length: 1 },   // index into ClassRegistry

  // Core Stats (base values before modifiers)
  baseStrength:     { type: Uint8Array, length: 1 }, // physical damage, carry weight
  baseDexterity:    { type: Uint8Array, length: 1 }, // attack speed, dodge, ranged accuracy
  baseIntelligence: { type: Uint8Array, length: 1 }, // magic damage, mana pool
  baseVitality:     { type: Uint8Array, length: 1 }, // HP pool, resistances
  baseWisdom:       { type: Uint8Array, length: 1 }, // mana regen, spell discount
  baseCharisma:     { type: Uint8Array, length: 1 }, // trading prices, NPC disposition

  // Derived resources (computed from stats + gear)
  mana:          { type: Float32Array, length: 1 },
  maxMana:       { type: Float32Array, length: 1 },
  stamina:       { type: Float32Array, length: 1 },
  maxStamina:    { type: Float32Array, length: 1 },
  manaRegen:     { type: Float32Array, length: 1 },

  // Faction & reputation
  factionId:     { type: Uint8Array,  length: 1 },
  reputation:    { type: Int32Array,  length: 8 },  // reputation per faction (-1000 to 1000)

  // Perk points
  perkPoints:    { type: Uint8Array,  length: 1 },
  skillPoints:   { type: Uint8Array,  length: 1 },
} as const satisfies ComponentDesc;
```

### 2.2 Classes

Classes define base stat allocation, starting skills, perk tree access, and special abilities. Each class is a data definition, not a class in the OOP sense.

```typescript
// /src/content/classes/ClassRegistry.ts

export const enum ClassId {
  WARRIOR,      // High strength/vitality, heavy armor, melee combat
  MAGE,         // High intelligence/wisdom, spellcasting, fragile
  ROGUE,        // High dexterity, stealth, bows, daggers, traps
  CLERIC,       // High wisdom, healing magic, buffs, holy damage
  RANGER,       // Balanced dexterity/wisdom, bows, animal companion, nature magic
  PALADIN,      // Strength/wisdom, heavy armor, holy magic, tank
  NECROMANCER,  // Intelligence/wisdom, dark magic, summons, curses
  BARD,         // Charisma/dexterity, buffs, debuffs, item crafting
}

export interface ClassDefinition {
  readonly id: ClassId;
  readonly name: string;
  readonly description: string;
  readonly baseStats: {
    readonly strength: number;
    readonly dexterity: number;
    readonly intelligence: number;
    readonly vitality: number;
    readonly wisdom: number;
    readonly charisma: number;
  };
  readonly statGrowthPerLevel: {
    readonly strength: number;
    readonly dexterity: number;
    readonly intelligence: number;
    readonly vitality: number;
    readonly wisdom: number;
    readonly charisma: number;
  };
  /** Starting skills the character knows. */
  readonly startingSkills: readonly number[];
  /** Starting equipment item IDs. */
  readonly startingEquipment: readonly number[];
  /** Perk tree IDs this class can access. */
  readonly perkTreeIds: readonly number[];
  /** Special ability unlocked at level 10. */
  readonly signatureAbilityId: number;
}

const CLASS_DEFINITIONS: ClassDefinition[] = [
  {
    id: ClassId.WARRIOR,
    name: "Warrior",
    description: "Masters of melee combat. Warriors charge into battle with heavy armor and powerful strikes.",
    baseStats: { strength: 14, dexterity: 10, intelligence: 6, vitality: 14, wisdom: 8, charisma: 8 },
    statGrowthPerLevel: { strength: 3, dexterity: 1.5, intelligence: 0.5, vitality: 2.5, wisdom: 1, charisma: 0.5 },
    startingSkills: [1, 2], // Slash, Block
    startingEquipment: [256, 272, 1, 5], // Wooden Sword, Wooden Axe, Stone, Wood Planks (block items)
    perkTreeIds: [1, 2], // Arms, Armor
    signatureAbilityId: 1001, // War Cry
  },
  {
    id: ClassId.MAGE,
    name: "Mage",
    description: "Wielders of arcane forces. Mages channel devastating spells but are vulnerable in close combat.",
    baseStats: { strength: 4, dexterity: 6, intelligence: 16, vitality: 6, wisdom: 14, charisma: 8 },
    statGrowthPerLevel: { strength: 0.5, dexterity: 1, intelligence: 3, vitality: 1, wisdom: 2.5, charisma: 1 },
    startingSkills: [10, 11], // Magic Bolt, Mana Shield
    startingEquipment: [300, 1], // Staff (bow item reimagined), Stone
    perkTreeIds: [3, 4], // Arcane, Elemental
    signatureAbilityId: 1002, // Meteor Shower
  },
  {
    id: ClassId.ROGUE,
    name: "Rogue",
    description: "Stealthy and swift. Rogues strike from the shadows with daggers and bows.",
    baseStats: { strength: 8, dexterity: 16, intelligence: 8, vitality: 8, wisdom: 6, charisma: 10 },
    statGrowthPerLevel: { strength: 1.5, dexterity: 3, intelligence: 1, vitality: 1.5, wisdom: 0.5, charisma: 1.5 },
    startingSkills: [20, 21], // Backstab, Stealth
    startingEquipment: [272, 320, 301], // Wooden Sword (reimagined as dagger), Shears (reimagined as lockpick), Arrow
    perkTreeIds: [5, 6], // Shadow, Precision
    signatureAbilityId: 1003, // Shadow Step
  },
  {
    id: ClassId.CLERIC,
    name: "Cleric",
    description: "Champions of the divine. Clerics heal allies, banish undead, and bolster armies.",
    baseStats: { strength: 10, dexterity: 6, intelligence: 10, vitality: 10, wisdom: 16, charisma: 12 },
    statGrowthPerLevel: { strength: 1.5, dexterity: 0.5, intelligence: 1.5, vitality: 2, wisdom: 3, charisma: 1.5 },
    startingSkills: [30, 31], // Heal, Smite
    startingEquipment: [256, 1, 5], // Mace (sword reimagined), Stone, Planks
    perkTreeIds: [7, 8], // Holy, Protection
    signatureAbilityId: 1004, // Divine Intervention
  },
  {
    id: ClassId.RANGER,
    name: "Ranger",
    description: "Masters of the wild. Rangers track prey, tame beasts, and wield nature's fury.",
    baseStats: { strength: 8, dexterity: 14, intelligence: 6, vitality: 10, wisdom: 12, charisma: 8 },
    statGrowthPerLevel: { strength: 1, dexterity: 2.5, intelligence: 0.5, vitality: 2, wisdom: 2.5, charisma: 1 },
    startingSkills: [40, 41], // Power Shot, Tracking
    startingEquipment: [288, 300, 301, 301, 301], // Bow, 3 Arrows (shovel reimagined as quiver)
    perkTreeIds: [9, 10], // Marksman, Nature
    signatureAbilityId: 1005, // Rain of Arrows
  },
  {
    id: ClassId.PALADIN,
    name: "Paladin",
    description: "Holy warriors who blend steel with divine magic. Paladins protect the innocent and smite evil.",
    baseStats: { strength: 12, dexterity: 8, intelligence: 6, vitality: 14, wisdom: 12, charisma: 10 },
    statGrowthPerLevel: { strength: 2.5, dexterity: 1, intelligence: 0.5, vitality: 2.5, wisdom: 2, charisma: 1.5 },
    startingSkills: [1, 30], // Slash, Heal (minor)
    startingEquipment: [272, 1, 5, 5], // Sword, Stone, Planks
    perkTreeIds: [2, 7], // Armor, Holy
    signatureAbilityId: 1006, // Holy Radiance
  },
  {
    id: ClassId.NECROMANCER,
    name: "Necromancer",
    description: "Masters of death. Necromancers raise undead armies, curse enemies, and drain life force.",
    baseStats: { strength: 4, dexterity: 6, intelligence: 16, vitality: 8, wisdom: 12, charisma: 6 },
    statGrowthPerLevel: { strength: 0.5, dexterity: 1, intelligence: 3, vitality: 1.5, wisdom: 2, charisma: 0.5 },
    startingSkills: [50, 51], // Life Drain, Raise Skeleton
    startingEquipment: [300, 359], // Staff, Nether Wart (reimagined as focus)
    perkTreeIds: [11, 12], // Death, Curses
    signatureAbilityId: 1007, // Army of the Dead
  },
  {
    id: ClassId.BARD,
    name: "Bard",
    description: "Musicians and tricksters. Bards inspire allies, confuse enemies, and craft exceptional items.",
    baseStats: { strength: 6, dexterity: 10, intelligence: 10, vitality: 8, wisdom: 8, charisma: 18 },
    statGrowthPerLevel: { strength: 0.5, dexterity: 2, intelligence: 1.5, vitality: 1, wisdom: 1.5, charisma: 3 },
    startingSkills: [60, 61], // Inspiring Melody, Dissonance
    startingEquipment: [300, 354, 355], // Instrument (bow reimagined), Feather, Flint
    perkTreeIds: [13, 14], // Performance, Crafting
    signatureAbilityId: 1008, // Encore
  },
];

export class ClassRegistry {
  private readonly classes = new Map<ClassId, ClassDefinition>();

  constructor() {
    for (const def of CLASS_DEFINITIONS) {
      this.classes.set(def.id, def);
    }
  }

  get(classId: ClassId): ClassDefinition {
    const def = this.classes.get(classId);
    if (!def) throw new Error(`Unknown class ${classId}`);
    return def;
  }

  getAll(): readonly ClassDefinition[] {
    return CLASS_DEFINITIONS;
  }

  /** Compute derived stats for a character at a given level. */
  computeStats(classId: ClassId, level: number): CharacterStats {
    const def = this.get(classId);
    const growth = def.statGrowthPerLevel;
    const lvl = level - 1; // level 1 uses base stats
    return {
      strength: Math.floor(def.baseStats.strength + growth.strength * lvl),
      dexterity: Math.floor(def.baseStats.dexterity + growth.dexterity * lvl),
      intelligence: Math.floor(def.baseStats.intelligence + growth.intelligence * lvl),
      vitality: Math.floor(def.baseStats.vitality + growth.vitality * lvl),
      wisdom: Math.floor(def.baseStats.wisdom + growth.wisdom * lvl),
      charisma: Math.floor(def.baseStats.charisma + growth.charisma * lvl),
      maxHp: 100 + Math.floor(def.baseStats.vitality * 10 + growth.vitality * lvl * 8),
      maxMana: def.baseStats.intelligence * 5 + Math.floor(growth.intelligence * lvl * 4),
      maxStamina: def.baseStats.dexterity * 5 + Math.floor(growth.dexterity * lvl * 4),
    };
  }
}

export interface CharacterStats {
  readonly strength: number;
  readonly dexterity: number;
  readonly intelligence: number;
  readonly vitality: number;
  readonly wisdom: number;
  readonly charisma: number;
  readonly maxHp: number;
  readonly maxMana: number;
  readonly maxStamina: number;
}
```

### 2.3 Experience & Leveling

```
Level Up Formula:
  XP required for level N = Floor(N ^ 1.8 + N * 50)

  Level  2:    152 XP
  Level  5:    418 XP
  Level 10:  1,131 XP
  Level 20:  4,347 XP
  Level 50:  36,274 XP
  Level 100: 208,509 XP

On level up:
  - All stats increase by class growth rates
  - HP, Mana, Stamina are recomputed from new stats
  - +1 perk point
  - +1 skill point (every other level, starting at level 2)
  - Unlock signature ability at level 10
```

```typescript
// /src/engine/ecs/systems/LevelingSystem.ts

export class LevelingSystem {
  static xpForLevel(level: number): number {
    return Math.floor(Math.pow(level, 1.8) + level * 50);
  }

  static addExperience(
    currentXp: number,
    currentLevel: number,
    amount: number,
  ): { newXp: number; newLevel: number; leveledUp: boolean } {
    let xp = currentXp + amount;
    let level = currentLevel;
    let leveledUp = false;

    while (level < 999 && xp >= LevelingSystem.xpForLevel(level + 1)) {
      level++;
      leveledUp = true;
    }

    return { newXp: xp, newLevel: level, leveledUp };
  }
}
```


---

## 3. Stats System

### 3.1 Core Stats

| Stat | Effect |
|:---|:---|
| **Strength** | +2 melee damage per 10 STR, +5 carry weight per STR, +1% block damage per STR |
| **Dexterity** | +1% attack speed per DEX, +0.5% dodge chance per DEX, +1 ranged damage per 5 DEX |
| **Intelligence** | +2 magic damage per 10 INT, +5 max mana per INT, spell unlock requirements |
| **Vitality** | +10 max HP per VIT, +0.5 HP regen per VIT, +1% poison resistance per VIT |
| **Wisdom** | +0.1 mana regen per WIS, -1% spell cost per WIS, +1% healing done per WIS |
| **Charisma** | -1% trading prices per CHA, +2 faction reputation gain per CHA, pet effectiveness |

### 3.2 Derived Stats

```typescript
export function computeDerivedStats(stats: CharacterStats): DerivedStats {
  return {
    maxHp: 100 + stats.vitality * 10,
    maxMana: stats.intelligence * 5,
    maxStamina: stats.dexterity * 5 + stats.strength * 3,
    hpRegen: stats.vitality * 0.5,
    manaRegen: stats.wisdom * 0.1,
    staminaRegen: stats.dexterity * 0.15 + stats.vitality * 0.05,
    meleeDamage: Math.floor(stats.strength * 0.2),
    rangedDamage: Math.floor(stats.dexterity * 0.2),
    magicDamage: Math.floor(stats.intelligence * 0.2),
    attackSpeed: 1.0 + stats.dexterity * 0.005,
    dodgeChance: Math.min(0.5, stats.dexterity * 0.005),
    spellCostReduction: Math.min(0.8, stats.wisdom * 0.005),
    carryWeight: stats.strength * 5 + 100,
  };
}
```

### 3.3 Perk Trees

Perks are passive or active abilities that define character specialization. Each class has unique trees, but some trees are shared (e.g., "Armor" is available to Warrior and Paladin).

```typescript
// /src/content/perks/PerkRegistry.ts

export const enum PerkTreeId {
  ARMS = 1,        // Warrior: weapon specialization
  ARMOR = 2,       // Warrior/Paladin: defensive passives
  ARCANE = 3,      // Mage: raw magic power
  ELEMENTAL = 4,   // Mage: fire, ice, lightning
  SHADOW = 5,      // Rogue: stealth, mobility
  PRECISION = 6,   // Rogue: critical hits, accuracy
  HOLY = 7,        // Cleric/Paladin: divine magic
  PROTECTION = 8,  // Cleric: shields, wards
  MARKSMAN = 9,    // Ranger: bow mastery
  NATURE = 10,     // Ranger: animal companion, plants
  DEATH = 11,      // Necromancer: undead army
  CURSES = 12,     // Necromancer: debuffs, life drain
  PERFORMANCE = 13,// Bard: songs, inspiration
  CRAFTING = 14,   // Bard: item creation, enchanting
}

export interface PerkDefinition {
  readonly id: number;
  readonly name: string;
  readonly description: string;
  readonly treeId: PerkTreeId;
  readonly tier: number;        // 0-5, how deep in the tree
  readonly requiredLevel: number;
  readonly requiredPerks: readonly number[]; // perk IDs that must be taken first
  readonly maxRank: number;     // 1-5, how many times it can be taken
  readonly modifiers: Partial<StatModifiers>; // what this perk changes
}

export interface StatModifiers {
  readonly meleeDamage: number;   // +% additive
  readonly magicDamage: number;
  readonly rangedDamage: number;
  readonly maxHp: number;
  readonly maxMana: number;
  readonly attackSpeed: number;
  readonly spellCost: number;     // -% reduction
  readonly dodgeChance: number;
  readonly criticalChance: number;
  readonly armorPenetration: number;
}

/**
 * Example perks:
 *
 * Tree: ARMS (Warrior)
 *   Tier 0: "Brawn" — +5% melee damage per rank (max 5)
 *   Tier 1: "Blade Mastery" — +10% attack speed with swords (max 3)
 *   Tier 1: "Shield Bash" — Unlock shield bash ability (max 1)
 *   Tier 2: "Executioner" — +20% damage to enemies below 50% HP (max 3)
 *   Tier 3: "Berserker" — +5% attack speed and +5% damage per enemy in melee range (max 2)
 *   Tier 4: "War Cry" — AOE fear, +20% damage for 10s (signature, 1 rank)
 *
 * Tree: ARCANE (Mage)
 *   Tier 0: "Arcane Affinity" — +5% magic damage per rank (max 5)
 *   Tier 1: "Mana Font" — +10% max mana per rank (max 3)
 *   Tier 1: "Quick Cast" — +5% cast speed per rank (max 3)
 *   Tier 2: "Spell Weaver" — 10% chance to dual-cast per rank (max 3)
 *   Tier 3: "Arcane Reach" — +20% spell range per rank (max 2)
 *   Tier 4: "Meteor Shower" — Rain of fire AoE (signature, 1 rank)
 */
```


---

## 4. Magic System

### 4.1 Magic Schools

Magic is organized into schools. Each school has a discipline level (0-100) that increases through use and unlocks higher-tier spells.

```typescript
// /src/content/magic/MagicRegistry.ts

export const enum MagicSchool {
  ARCANE,     // Raw magic — bolts, shields, teleportation
  FIRE,       // Combustion — fireballs, burns, heat
  ICE,        // Frost — freezing, slowing, ice walls
  LIGHTNING,  // Electricity — chain lightning, stuns, speed
  HOLY,       // Divine — healing, protection, undead bane
  DARK,       // Shadow — life drain, fear, curses
  NATURE,     // Wild — growth, poison, animal empathy
  EARTH,      // Terrain — walls, tremors, gravity
  AIR,        // Wind — levitation, gusts, invisibility
  WATER,      // Fluid — healing waves, drowning, purification
}

export interface SpellDefinition {
  readonly id: number;
  readonly name: string;
  readonly description: string;
  readonly school: MagicSchool;
  readonly tier: number;              // 1-5, spell tier
  readonly manaCost: number;
  readonly castTime: number;          // seconds, 0 = instant
  readonly cooldown: number;          // seconds
  readonly requiredLevel: number;     // character level
  readonly requiredDiscipline: number; // school discipline level
  readonly baseDamage?: number;
  readonly baseHealing?: number;
  readonly duration?: number;         // seconds
  readonly range: number;            // blocks
  readonly aoeRadius?: number;       // blocks, 0 = single target
  /** Effects applied by this spell. */
  readonly effects: readonly SpellEffect[];
}

export const enum SpellEffectType {
  DAMAGE,
  HEAL,
  DOT,          // Damage over time
  HOT,          // Healing over time
  BUFF,         // Stat boost
  DEBUFF,       // Stat reduction
  STUN,
  SLOW,
  FEAR,
  TELEPORT,
  SUMMON,
  SHIELD,
  MANA_RESTORE,
}

export interface SpellEffect {
  readonly type: SpellEffectType;
  readonly magnitude: number;
  readonly duration?: number;
  readonly tickInterval?: number;
  readonly chance: number; // 0.0-1.0
}

// --- Example Spells ---

const SPELLS: SpellDefinition[] = [
  // Arcane
  { id: 10, name: "Arcane Bolt", description: "Fires a bolt of pure magic.", school: MagicSchool.ARCANE, tier: 1, manaCost: 10, castTime: 0.5, cooldown: 0.5, requiredLevel: 1, requiredDiscipline: 0, baseDamage: 15, range: 32, effects: [{ type: SpellEffectType.DAMAGE, magnitude: 15, chance: 1.0 }] },
  { id: 11, name: "Mana Shield", description: "Conjure a shield that absorbs damage using mana.", school: MagicSchool.ARCANE, tier: 2, manaCost: 30, castTime: 1.0, cooldown: 5.0, requiredLevel: 5, requiredDiscipline: 20, duration: 30, range: 0, effects: [{ type: SpellEffectType.SHIELD, magnitude: 50, duration: 30, chance: 1.0 }] },
  { id: 12, name: "Blink", description: "Short-range teleport in the direction you're facing.", school: MagicSchool.ARCANE, tier: 3, manaCost: 25, castTime: 0.2, cooldown: 8.0, requiredLevel: 10, requiredDiscipline: 40, range: 16, effects: [{ type: SpellEffectType.TELEPORT, magnitude: 16, chance: 1.0 }] },

  // Fire
  { id: 20, name: "Fireball", description: "Launches a ball of fire that explodes on impact.", school: MagicSchool.FIRE, tier: 2, manaCost: 20, castTime: 1.0, cooldown: 2.0, requiredLevel: 5, requiredDiscipline: 15, baseDamage: 25, range: 24, aoeRadius: 3, effects: [{ type: SpellEffectType.DAMAGE, magnitude: 25, chance: 1.0 }, { type: SpellEffectType.DOT, magnitude: 5, duration: 6, tickInterval: 2, chance: 0.8 }] },
  { id: 21, name: "Flame Wall", description: "Creates a wall of fire that burns enemies who pass through.", school: MagicSchool.FIRE, tier: 4, manaCost: 50, castTime: 2.0, cooldown: 15.0, requiredLevel: 20, requiredDiscipline: 60, range: 16, duration: 10, effects: [{ type: SpellEffectType.DOT, magnitude: 12, duration: 10, tickInterval: 1, chance: 1.0 }] },

  // Ice
  { id: 30, name: "Frost Bolt", description: "Slows and damages a target with ice.", school: MagicSchool.ICE, tier: 1, manaCost: 12, castTime: 0.8, cooldown: 1.0, requiredLevel: 3, requiredDiscipline: 10, baseDamage: 12, range: 28, effects: [{ type: SpellEffectType.DAMAGE, magnitude: 12, chance: 1.0 }, { type: SpellEffectType.SLOW, magnitude: 0.4, duration: 5, chance: 1.0 }] },

  // Holy
  { id: 40, name: "Heal", description: "Restores health to a target.", school: MagicSchool.HOLY, tier: 1, manaCost: 15, castTime: 1.0, cooldown: 1.0, requiredLevel: 1, requiredDiscipline: 0, baseHealing: 20, range: 16, effects: [{ type: SpellEffectType.HEAL, magnitude: 20, chance: 1.0 }] },
  { id: 41, name: "Smite", description: "Holy damage to undead and dark creatures.", school: MagicSchool.HOLY, tier: 2, manaCost: 15, castTime: 0.8, cooldown: 2.0, requiredLevel: 5, requiredDiscipline: 20, baseDamage: 30, range: 24, effects: [{ type: SpellEffectType.DAMAGE, magnitude: 30, chance: 1.0 }] },

  // Dark
  { id: 50, name: "Life Drain", description: "Drains life from a target, healing you for the damage dealt.", school: MagicSchool.DARK, tier: 2, manaCost: 20, castTime: 1.0, cooldown: 3.0, requiredLevel: 5, requiredDiscipline: 20, baseDamage: 18, range: 20, effects: [{ type: SpellEffectType.DAMAGE, magnitude: 18, chance: 1.0 }, { type: SpellEffectType.HEAL, magnitude: 18, chance: 1.0 }] },
  { id: 51, name: "Raise Skeleton", description: "Raises a skeleton from a corpse to fight for you.", school: MagicSchool.DARK, tier: 3, manaCost: 40, castTime: 2.0, cooldown: 10.0, requiredLevel: 10, requiredDiscipline: 35, range: 8, duration: 60, effects: [{ type: SpellEffectType.SUMMON, magnitude: 1, duration: 60, chance: 1.0 }] },

  // Nature
  { id: 60, name: "Poison Arrow", description: "Fires a poison-tipped projectile.", school: MagicSchool.NATURE, tier: 2, manaCost: 15, castTime: 0.8, cooldown: 2.0, requiredLevel: 5, requiredDiscipline: 15, baseDamage: 10, range: 28, effects: [{ type: SpellEffectType.DOT, magnitude: 6, duration: 8, tickInterval: 2, chance: 1.0 }] },

  // Earth
  { id: 70, name: "Stone Wall", description: "Raises a wall of stone from the ground.", school: MagicSchool.EARTH, tier: 3, manaCost: 35, castTime: 2.0, cooldown: 20.0, requiredLevel: 10, requiredDiscipline: 30, range: 16, duration: 20, effects: [{ type: SpellEffectType.BUFF, magnitude: 0, duration: 20, chance: 1.0 }] }, // handled by world block placement
];
```

### 4.2 Casting Mechanics

Casting follows a three-phase flow:


1. **Initiation** — Player selects spell (hotbar or spell book UI), mana cost is checked.
2. **Channel/Cast Time** — Player must stand still (or move slowly) for the cast duration. Interruption resets the cast.
3. **Release** — Spell effect is executed: projectile spawned, ray traced, AoE applied, buff applied.

```typescript
// /src/engine/ecs/systems/SpellCastingSystem.ts

/**
 * Processes active spell casts each frame.
 * Handles cast time tracking, animation state, and spell release.
 */
export class SpellCastingSystem {
  // Per-player cast state stored in a small ring buffer
  private readonly activeCasts = new Map<number, ActiveCast>();

  startCast(playerEntity: number, spell: SpellDefinition): CastResult {
    // Check mana
    // Check line of sight (if required)
    // Begin cast timer
    // Return success/failure
  }

  update(dt: number): void {
    // Advance cast timers
    // On completion: execute spell effects
    // On interruption: cancel cast, refund partial mana
  }

  cancelCast(playerEntity: number): void {
    // Interrupt active cast
  }
}

export interface ActiveCast {
  readonly playerEntity: number;
  readonly spell: SpellDefinition;
  elapsed: number;
  completed: boolean;
}
```

### 4.3 Discipline Leveling

Each magic school has a discipline level that increases through use:

```
Discipline XP per spell cast = manaCost * (1 + spellTier * 0.5)
Discipline level N requires = N * 100 XP

Example: Casting Fireball (tier 2, 20 mana) gives 20 * (1 + 2*0.5) = 40 discipline XP.
A level 10 Fire discipline requires 10 * 100 = 1000 XP, or ~25 fireball casts.
```


---

## 5. Quest System

### 5.1 Quest Definition

Quests are data-driven state machines with objectives, rewards, and prerequisites.

```typescript
// /src/content/quests/QuestRegistry.ts

export const enum QuestObjectiveType {
  TALK_TO_NPC,
  KILL_MOBS,
  GATHER_ITEMS,
  REACH_LOCATION,
  CRAFT_ITEM,
  BUILD_STRUCTURE,
  USE_SPELL,
  ESCORT_NPC,
  DELIVER_ITEM,
  ACTIVATE_OBJECT,
}

export const enum QuestRewardType {
  EXPERIENCE,
  ITEM,
  GOLD,
  REPUTATION,
  SPELL_UNLOCK,
  PERK_POINT,
  SKILL_POINT,
}

export interface QuestObjective {
  readonly type: QuestObjectiveType;
  readonly description: string;
  /** Target entity ID, item ID, block ID, or location. */
  readonly targetId: number;
  /** Required count (for kill/gather/craft objectives). */
  readonly required: number;
  /** Progress tracked in quest state. */
  readonly progress: number;
}

export interface QuestReward {
  readonly type: QuestRewardType;
  readonly id: number;      // item ID, faction ID, spell ID, etc.
  readonly amount: number;
}

export interface QuestDefinition {
  readonly id: number;
  readonly name: string;
  readonly description: string;
  readonly requiredLevel: number;
  readonly requiredQuests: readonly number[]; // quest IDs that must be completed first
  readonly factionId?: number;  // optional faction affiliation
  readonly minimumReputation?: number; // minimum reputation with faction
  readonly objectives: readonly QuestObjective[];
  readonly rewards: readonly QuestReward[];
  readonly failureConditions?: readonly QuestObjective[]; // optional fail conditions
}

export enum QuestState {
  NOT_STARTED,
  ACTIVE,
  COMPLETED,
  FAILED,
  TURNED_IN,
}
```

### 5.2 Example Quests

| Quest | Level | Objectives | Rewards |
|:---|:---|:---|:---|
| **A Farmer's Plight** | 1 | Talk to Farmer John (0/1), Kill 8 Vile Rats (0/8) | 100 XP, 5 Bread |
| **The Missing Merchant** | 5 | Find merchant in Dark Woods (0/1), Defeat 3 Bandits (0/3), Report back | 500 XP, Iron Sword |
| **Corruption in the Mines** | 10 | Clear the Abandoned Mines (0/1), Kill 15 Crystal Zombies (0/15), Collect 3 Purple Crystals (0/3) | 2000 XP, Ring of Protection, +100 Reputation with Town |
| **Ritual of the Phoenix** | 20 (Mage) | Gather Phoenix Feather (0/1), Craft Scroll of Rebirth (0/1), Cast at Sun Altar (0/1) | 5000 XP, Unlock "Rebirth" Spell, +200 Rep Mages Guild |
| **The Bandit King** | 15 | Scout Bandit Fort (0/1), Poison Supply Cache (0/1), Defeat Bandit King (0/1) | 4000 XP, 50 Gold, Bandit King's Dagger |
| **A Temple Lost** | 25 | Find Temple Entrance (0/1), Solve 3 Light Puzzles (0/3), Defeat Temple Guardian (0/1) | 10000 XP, Ancient Relic, +500 Rep Historians |

### 5.3 Quest Tracking System

```typescript
// /src/engine/ecs/components/QuestLogComponent.ts

export const QuestLogDesc = {
  // Bitfield of active quests (up to 128 tracked simultaneously)
  activeQuests: { type: Uint32Array, length: 4 },  // 128 bits
  // Bitfield of completed quests
  completedQuests: { type: Uint32Array, length: 16 }, // 512 bits
  // Objective progress (up to 8 objectives per quest, up to 16 quests with progress)
  objectiveProgress: { type: Uint16Array, length: 128 },
} as const satisfies ComponentDesc;
```


---

## 6. Economy & Trading

### 6.1 Currency

Gold coins are the universal currency. One gold coin is item ID 900 (or a dedicated currency component).

```
1 Gold Coin (item 900) = base currency
1 Silver Coin (item 901) = 1/100 gold
1 Copper Coin (item 902) = 1/100 silver
```

### 6.2 NPC Trading System

NPCs have buy/sell lists with prices modulated by the player's charisma and faction reputation.

```typescript
// /src/content/economy/TradeRegistry.ts

export interface TradeEntry {
  readonly itemId: number;
  /** Base buy price (NPC sells to player). */
  readonly buyPrice: number;
  /** Base sell price (NPC buys from player, typically 20-50% of buy). */
  readonly sellPrice: number;
  /** Stock limit (-1 = unlimited). */
  readonly stock: number;
  /** How many game days between restocks. */
  readonly restockDays: number;
}

export interface NPCShopDefinition {
  readonly npcId: number;
  readonly npcName: string;
  readonly trades: readonly TradeEntry[];
  /** Faction ID for reputation-based pricing. */
  readonly factionId?: number;
  /** Reputation threshold for special discounts. */
  readonly reputationDiscounts: readonly ReputationDiscount[];
}

export interface ReputationDiscount {
  readonly threshold: number;   // min reputation
  readonly discount: number;    // +% sell price, -% buy price
  readonly title: string;       // e.g., "Friend", "Honored", "Exalted"
}

/**
 * Calculate effective price based on charisma and reputation.
 *
 * Formula:
 *   charismaMod = 1.0 - (charisma * 0.01)  // 10 CHA = 10% discount
 *   repMod = 1.0 - (reputationDiscount)     // Friend = 5%, Honored = 10%, Exalted = 20%
 *   buyPrice = baseBuyPrice * charismaMod * repMod
 *   sellPrice = baseSellPrice * (2.0 - charismaMod) * (1.0 + repModReduction)
 */
export function calculatePrice(
  basePrice: number,
  charisma: number,
  reputationDiscount: number,
  isBuying: boolean,
): number {
  const chaMod = 1.0 - Math.min(0.5, charisma * 0.01);
  const repMod = 1.0 - reputationDiscount;

  if (isBuying) {
    return Math.ceil(basePrice * chaMod * repMod);
  } else {
    // Selling to NPC — charisma and rep increase sell price
    return Math.floor(basePrice * (2.0 - chaMod) * (1.0 + reputationDiscount * 0.5));
  }
}
```

### 6.3 Faction Reputation

```typescript
// /src/content/factions/FactionRegistry.ts

export const enum FactionId {
  TOWN_GUARD,
  MERCHANTS_GUILD,
  MAGES_GUILD,
  THIEVES_GUILD,
  FOREST_WARDENS,
  DARK_COVEN,
  HISTORIANS,
  ARENA,
}

export interface FactionDefinition {
  readonly id: FactionId;
  readonly name: string;
  readonly description: string;
  readonly hostileFactions: readonly FactionId[];
  readonly alliedFactions: readonly FactionId[];
  /** List of titles earned at reputation thresholds. */
  readonly titles: readonly ReputationTitle[];
}

export interface ReputationTitle {
  readonly threshold: number;
  readonly title: string;
}

/**
 * Reputation scale:
 *   -1000 to -501:  Hated
 *   -500 to -101:   Hostile
 *   -100 to -1:     Unfriendly
 *   0 to 99:        Neutral
 *   100 to 499:     Friendly
 *   500 to 999:     Honored
 *   1000:           Exalted
 */
```


---

## 7. NPC & Faction AI

### 7.1 NPC Schedule System

NPCs follow daily schedules that determine their position and behavior. This is powered by the existing `AIState` component and pathfinding system.

```typescript
// /src/content/npc/ScheduleSystem.ts

export interface ScheduleEntry {
  readonly hour: number;       // 0-24 game time
  readonly minute: number;
  readonly action: NpcAction;
  readonly targetX: number;
  readonly targetY: number;
  readonly targetZ: number;
  readonly duration: number;   // minutes to perform the action
}

export const enum NpcAction {
  SLEEP,
  WORK,         // At shop counter, at crafting station
  WANDER,       // Patrol within a defined area
  EAT,          // At tavern, at home
  TALK,         // Socialize with other NPCs at a gathering point
  GUARD,        // Stand at a fixed post
  FLEE,         // Run to shelter (triggered by events)
  FIGHT,        // Combat mode
  TRAVEL,       // Move to another location (inter-town)
}

/**
 * Example: Blacksmith daily schedule
 *   06:00 — Wake up, leave home
 *   06:30 — Arrive at forge, start WORK
 *   12:00 — EAT at tavern (30 min)
 *   12:30 — Return to forge, WORK
 *   18:00 — Close shop, TRAVEL home
 *   18:30 — EAT at home
 *   19:00 — WANDER around town
 *   21:00 — SLEEP
 */
```

### 7.2 Faction Territory & Influence

The world map is divided into faction-controlled regions. Each chunk tracks which faction claims it.

```typescript
// Extension to ChunkColumn or a new overlay
export interface FactionClaim {
  readonly factionId: number;
  readonly influence: number;      // 0-100, how strong the claim is
  readonly lastClaimUpdate: number; // game time of last influence change
}

/**
 * Influence changes based on:
 *   - Player actions (quests, trading) in the region
 *   - NPC patrols and presence
 *   - Building construction (faction outposts)
 *   - Hostile actions (killing faction members)
 *
 * At influence >= 50, the faction controls the chunk.
 * Controlled chunks affect:
 *   - Which NPCs spawn there
 *   - Building permissions (player vs faction)
 *   - Tax rates for player-owned shops
 *   - Fast travel point availability
 */
```


---

## 8. Skill System

### 8.1 Skills

Skills are secondary progression that improves through use. Unlike class stats, skills are trained by performing specific actions.

```typescript
// /src/content/skills/SkillRegistry.ts

export const enum SkillId {
  // Combat
  ONE_HANDED,     // Swords, daggers, maces
  TWO_HANDED,     // Greatswords, axes, hammers
  ARCHERY,        // Bows, crossbows
  BLOCKING,       // Shields, parrying
  UNARMED,        // Fists, claws

  // Magic
  ARCANE_MAGIC,
  FIRE_MAGIC,
  ICE_MAGIC,
  HOLY_MAGIC,
  DARK_MAGIC,
  NATURE_MAGIC,

  // Crafting
  SMITHING,       // Metal tools, weapons, armor
  WOODWORKING,    // Wooden items, bows, furniture
  ALCHEMY,        // Potions, poisons, brews
  ENCHANTING,     // Item enchantments, scrolls
  COOKING,        // Food, buff meals
  JEWELRY,        // Rings, amulets, gems

  // Gathering
  MINING,         // Ore, stone, gems
  FORAGING,       // Plants, herbs, mushrooms
  HUNTING,        // Animal drops, hides, meat
  FISHING,        // Fish, treasure, junk
  WOODCUTTING,    // Logs, bark

  // General
  STEALTH,
  LOCKPICKING,
  PERSUASION,
  TRADING,
}

export interface SkillDefinition {
  readonly id: SkillId;
  readonly name: string;
  readonly description: string;
  /** Skill level cap (0-100). */
  readonly maxLevel: number;
  /** XP required per level = level * baseXpPerLevel. */
  readonly baseXpPerLevel: number;
  /** Which stat governs XP gain rate. */
  readonly governingStat: keyof CharacterStats;
  /** Unlockable perks at certain skill thresholds. */
  readonly perks: readonly SkillPerk[];
}

export interface SkillPerk {
  readonly requiredLevel: number;
  readonly perkId: number;
}

/**
 * Skill XP Formula:
 *   baseXP = baseXpPerLevel * skillLevel
 *   governingStatMod = 1 + (governingStat / 100)
 *   actualXPRequired = baseXP / governingStatMod
 *
 * Leveling: skillXP += actionXP * governingStatMod
 * When skillXP >= requiredXP, skill level increases and XP resets.
 */
```


---

## 9. World Integration

### 9.1 Structure Overhaul

Existing structures (villages) are replaced with RPG-relevant points of interest:

| Structure | Content | Difficulty |
|:---|:---|:---|
| **Town** | NPCs with shops, quest givers, inn, guild halls, player housing | Safe |
| **Fortress** | Faction-controlled stronghold, guards, commander with quests | Medium |
| **Dungeon** | Procedural rooms, boss at the end, loot chests, traps | High |
| **Ruins** | Collapsed buildings, puzzles, treasure, mini-bosses | Medium |
| **Cave System** | Ore veins, monster spawners, underground lakes, rare gems | Variable |
| **Tower** | Wizard's tower, spell scrolls, golem guardians | High |
| **Shrine** | Healing pool, blessing buff, neutral territory | Safe |
| **Camp** | Bandit camp, merchant caravan, temporary NPCs | Low |
| **Graveyard** | Undead spawns, haunted, rare dark magic items | Medium |
| **Arena** | Combat challenges, betting, faction reputation | Variable |

### 9.2 Procedural Town Generation

```typescript
// /src/engine/workers/worldgen/TownPlanner.ts

/**
 * Generates a town layout using a simple plot-based system:
 *
 * 1. Find a flat area (similar to village placement)
 * 2. Define town center (well/fountain)
 * 3. Place roads in a grid pattern radiating from center
 * 4. Assign plots:
 *    - Shops along main road (blacksmith, alchemist, general store)
 *    - Houses on side roads (NPC homes)
 *    - Guild halls near center
 *    - Inn near center
 *    - Guard post at entrances
 *    - Temple at a prominent location
 * 5. Generate NPCs with roles matching the buildings
 * 6. Assign schedules based on building types
 * 7. Set faction claim for the town area
 *
 * Town size scales with region difficulty:
 *   - Safe regions (near spawn): 1-3 buildings
 *   - Mid regions: 4-8 buildings with walls
 *   - High difficulty: fortress towns with full wall enclosures
 */
```

### 9.3 Loot Tables

```typescript
// /src/content/loot/LootRegistry.ts

export interface LootEntry {
  readonly itemId: number;
  readonly weight: number;     // probability weight
  readonly minCount: number;
  readonly maxCount: number;
  readonly minLevel?: number;  // minimum player level for this drop
}

export interface LootTable {
  readonly id: number;
  readonly entries: readonly LootEntry[];
  readonly rolls: number;     // how many times to roll
  readonly bonusRolls?: number; // extra rolls per luck point
}

const LOOT_TABLES: Record<string, LootTable> = {
  "zombie_drop": {
    id: 1,
    rolls: 1,
    entries: [
      { itemId: 0, weight: 40, minCount: 0, maxCount: 0 },         // nothing
      { itemId: 595, weight: 30, minCount: 1, maxCount: 2 },       // rotten flesh
      { itemId: 347, weight: 10, minCount: 1, maxCount: 1 },       // gunpowder
      { itemId: 115, weight: 10, minCount: 1, maxCount: 1 },       // iron ingot (rare)
      { itemId: 340, weight: 8, minCount: 1, maxCount: 1 },        // iron ingot
      { itemId: 362, weight: 2, minCount: 1, maxCount: 1 },        // ender pearl (ultra rare)
    ],
  },
  "dungeon_chest": {
    id: 2,
    rolls: 3,
    bonusRolls: 1,
    entries: [
      { itemId: 340, weight: 30, minCount: 1, maxCount: 3 },       // iron ingots
      { itemId: 341, weight: 15, minCount: 1, maxCount: 2 },       // gold ingots
      { itemId: 116, weight: 20, minCount: 2, maxCount: 5 },       // bread
      { itemId: 328, weight: 10, minCount: 1, maxCount: 1 },       // flint & steel
      { itemId: 330, weight: 8, minCount: 1, maxCount: 1 },        // compass
      { itemId: 342, weight: 5, minCount: 1, maxCount: 1 },        // diamond
      { itemId: 580, weight: 3, minCount: 1, maxCount: 1 },        // golden apple
      { itemId: 591, weight: 5, minCount: 1, maxCount: 1 },        // golden carrot
    ],
  },
};
```


---

## 10. UI & Interaction Overhaul

### 10.1 New HUD Layout

```
┌─────────────────────────────────────────────────────┐
│ [HP Bar ████████░░] [MP Bar ██████░░░░] [SP Bar ███]│
│                                                     │
│                  (world viewport)                    │
│                                                     │
│                                                     │
│                  [Quest Tracker]                     │
│                   - A Farmer's Plight               │
│                   - Talk to Farmer John              │
│                   - Kill Vile Rats 5/8              │
│                                                     │
│ [Hotbar: S1][S2][S3][S4][S5][S6][S7][S8][S9]       │
│ [Gold: 42g 15s 8c]                   [Level 12 War] │
└─────────────────────────────────────────────────────┘
```

### 10.2 Character Sheet UI

The existing inventory panel is extended with tabs:

| Tab | Content |
|:---|:---|
| **Inventory** | Existing 45-slot inventory |
| **Character** | Stats, level, XP bar, class title, equipped gear |
| **Skills** | Skill list with level and progress bars |
| **Magic** | Known spells, mana cost display, discipline levels |
| **Perks** | Perk trees with visual tree layout, spend perk points |
| **Quests** | Active quest list with objectives and rewards |
| **Map** | World map with discovered locations, faction territories |

### 10.3 Interaction with NPCs

Right-clicking an NPC opens a context menu:

```
┌────────────────────┐
│ [Merchant Title]   │
│ ─────────────────  │
│ Talk                │
│ Trade               │
│ ─────────────────  │
│ [Quest] The Missing │
│   Merchant          │
│ ─────────────────  │
│ Leave               │
└────────────────────┘
```


---

## 11. Renaming Convention — Moving Away From Minecraft

To establish the game as its own identity, all Minecraft-derived names, terms, and concepts are replaced:

| Minecraft Term | Replacement |
|:---|:---|
| Minecraft | *project name TBD* |
| Steve / Alex | "Adventurer" |
| Survival Mode | "Adventure Mode" |
| Creative Mode | "Builder Mode" |
| Hardcore Mode | "Iron Mode" |
| Health / Hunger | "Health / Energy" |
| Experience / XP | "Experience / XP" |
| Enchanting | "Infusion" |
| Nether | "The Void" |
| End / Ender Dragon | "The Rift / Void Tyrant" |
| Redstone | "Essence Conduit" / "Arcane Wire" |
| Pig, Cow, Sheep, Chicken | Wild Boar, Meadow Ox, Fluffox, Cliffhen |
| Zombie, Skeleton, Creeper, Spider | Husks, Bonereapers, Wisps, Shadow Weavers |
| Villager | "Townsperson" / "Citizen" |
| Iron Golem | "Guardian Construct" |
| Enderman | "Rift Walker" |
| Blaze | "Infernal Spirit" |
| Ghast | "Void Jelly" |
| Wooden/Stone/Iron/Gold/Diamond | "Primitive/Flint/Bronze/Imperial/Crystal" (tiers) |
| Pickaxe/Axe/Shovel/Hoe | "Excavator/Chopper/Digger/Tiller" |
| Sword/Bow/Arrow | "Blade/Bow/Bolt" |
| Wheat / Carrots / Potatoes | "Golden Grain / Sunroot / Earthapple" |
| Crafting Table | "Workbench" |
| Furnace | "Forge" |
| Chest | "Storage Crate" |
| Bed | "Bedroll" |
| Torch | "Glow Lantern" |


---

## 12. Technical Integration

### 12.1 New ECS Components

```
src/engine/ecs/components/
├── CharacterComponent.ts     — RPG character stats, class, level, XP
├── QuestLogComponent.ts      — Active/completed quest tracking
├── SpellBookComponent.ts     — Known spells, active cooldowns
├── SkillComponent.ts         — Skill levels and XP per skill
├── FactionComponent.ts       — Faction membership and reputation
├── HungerComponent.ts        — Energy/consumption system
├── EquipmentComponent.ts     — Equipped gear slots (head, chest, legs, feet, main hand, off hand, ring, necklace)
└── NpcScheduleComponent.ts   — Daily schedule state for NPCs
```

### 12.2 New ECS Systems

```
src/engine/ecs/systems/
├── LevelingSystem.ts          — XP accumulation, level-up, stat growth
├── SpellCastingSystem.ts      — Cast timer, mana check, spell execution
├── DisciplineSystem.ts        — Magic school discipline XP tracking
├── SkillSystem.ts             — Skill XP gain, level-up perks
├── QuestTrackingSystem.ts     — Objective progress, quest completion
├── NpcScheduleSystem.ts       — NPC daily schedule execution
├── FactionInfluenceSystem.ts  — Territory influence updates
├── TradingSystem.ts           — Buy/sell transactions, price calculation
├── DialogueSystem.ts          — NPC dialogue tree navigation
├── LootSystem.ts              — Loot table rolls, item spawning
└── ReputationSystem.ts        — Faction reputation changes from actions
```

### 12.3 WorldGen Integration

```
WorldGenWorker.generate()
├── 1. Terrain heightmap & 3D noise density
├── 2. Biome surface rules
├── 3. Cave carving
├── 4. Ore distribution
├── 5. [MODIFIED] Structure placement
│   ├── Region difficulty evaluation (distance from spawn)
│   ├── Town/fortress/dungeon placement (scaled by difficulty)
│   ├── Procedural building generation per structure type
│   └── NPC spawn point registration
├── 6. Flora decoration
└── 7. Faction claim initialization
```

### 12.4 New Content Files

```
src/content/
├── classes/
│   ├── ClassRegistry.ts       — 8 classes with stats, growth, perks
├── magic/
│   ├── MagicRegistry.ts       — 10 schools, spell definitions
│   ├── MagicSchool.ts         — School enum
│   └── SpellEffect.ts         — Effect types and application
├── quests/
│   ├── QuestRegistry.ts       — Quest definitions
│   ├── QuestState.ts          — State machine
│   └── QuestGenerator.ts      — Procedural quest generation
├── factions/
│   ├── FactionRegistry.ts     — Faction definitions, titles
│   └── ReputationSystem.ts    — Reputation math
├── economy/
│   ├── TradeRegistry.ts       — NPC shop definitions
│   ├── Currency.ts            — Gold/silver/copper handling
│   └── PricingSystem.ts       — Price calculation with modifiers
├── skills/
│   ├── SkillRegistry.ts       — Skill definitions, governing stats
│   └── SkillActionMap.ts      — Maps actions to skill XP gains
├── perks/
│   ├── PerkRegistry.ts        — Perk definitions
│   └── PerkTree.ts            — Tree structure and validation
├── loot/
│   ├── LootRegistry.ts        — Loot tables
│   └── LootRoller.ts          — Weighted random selection
├── npc/
│   ├── NpcRegistry.ts         — NPC definitions, roles, schedules
│   ├── DialogueRegistry.ts    — Dialogue trees
│   └── ScheduleSystem.ts      — Daily schedule execution
└── naming/
    ├── NameGenerator.ts       — Procedural name generation for NPCs, locations, items
    └── Terminology.ts         — Centralized renaming map from MC conventions
```


---

## 13. Summary of Architectural Changes

### What Stays

* Chunked terrain world with `SharedPool` zero-copy workers
* ECS with SoA `TypedArray` storage
* WebGL2 renderer with SurfaceNets meshing, two-pass transparency, texture arrays
* Camera, physics, collision, pathfinding systems
* Material registry and factory pattern
* Deterministic world generation
* Inventory and crafting (extended)

### What Changes

* **Block/Item IDs** — Expanded beyond MC conventions, custom block types
* **World generation** — Structure placement becomes POI-driven (towns, dungeons, ruins)
* **Player entity** — Gains `CharacterComponent` with stats, class, perks, quest log, spell book
* **Mob system** — Replaced with RPG creature types, scaling by region difficulty
* **UI** — Character sheet, skill trees, spell book, quest tracker, minimap replaced
* **Interaction** — NPC dialogue, trading, quest giver integration
* **Naming** — All MC terms replaced with original IP

### What's New

* Magic system with 10 schools, 50+ spells, discipline leveling
* Quest system with objectives, rewards, prerequisites, state tracking
* Faction system with reputation, territory, titles
* Economy with NPC trading, currency, price modulation
* Skill system with 20 skills trained through use
* Perk trees with 14 trees, 100+ perks
* NPC daily schedules and roles
* Procedural quest and loot generation
* Region difficulty scaling


