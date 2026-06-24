# Stat System — Comprehensive Attribute & Derivation Framework

**Version:** 1.0  
**Scope:** All character statistics: primary attributes, secondary/derived stats, combat stats, magic stats, utility stats, diminishing returns, temporary modifiers, buff/debuff stacking, and the complete formula reference.  
**Architecture Constraints:** Strict TypeScript, DOD-based ECS with SoA TypedArray storage, deterministic computations, no floating-point ambiguity in critical paths.

---

## 1. System Overview

Stats are the numeric foundation of every character. They determine how much damage a warrior deals, how fast a rogue moves, how much mana a mage commands, and whether a lucky halfling finds treasure instead of a trap.

### Stat Layers

```
Primary Attributes (8)     ← Species + Class + Background + Level
  ├── Strength
  ├── Dexterity
  ├── Constitution
  ├── Intelligence
  ├── Wisdom
  ├── Charisma
  ├── Luck
  └── Perception

Secondary Derived (20+)    ← Computed from primaries + gear + buffs
  ├── Combat: meleeDamage, rangedDamage, magicDamage, attackSpeed, armorPenetration
  ├── Defense: maxHp, armor, toughness, dodgeChance, blockChance, resilience
  ├── Magic: maxMana, manaRegen, spellPower, spellCostReduction, cooldownReduction
  ├── Recovery: hpRegen, staminaRegen, restBonus
  ├── Utility: carryWeight, moveSpeed, jumpHeight, stealthRating
  └── Luck: criticalChance, criticalDamage, lootQuality, salvageYield

Temporary Modifiers        ← Buffs, debuffs, potions, enchants, environment
  └── Stack via rules: additive, multiplicative, or take-highest
  
Skill Levels (20+)         ← Trained through use, capped at 100
  └── Each skill governed by 1-2 primary attributes
```

### Design Rules

1. **All formulas are deterministic** — same inputs always produce same outputs.
2. **No branching in hot paths** — stat lookups are flat integer/float array accesses.
3. **Diminishing returns** — extreme values are clamped via smooth curves, not hard caps.
4. **Modifier stacking** — buffs/debuffs follow typed stacking rules (add, multiply, highest).
5. **Float-precision safe** — all combat-relevant values use integer thresholds where possible.

---

## 2. Primary Attributes

### 2.1 Attribute Definitions

```typescript
// /src/content/stats/PrimaryAttributes.ts

/**
 * The eight primary attributes.
 * Every character has a base value for each, determined by
 * species + class + background + level. Values typically range 1-100
 * for normal characters, with epic characters reaching 150+.
 *
 * Attribute values are stored as Uint8 in the CharacterComponent
 * (range 0-255), then expanded to float for derived computations.
 */
export const enum Attribute {
  /** Physical power: melee damage, carry weight, break speed. */
  STRENGTH = 0,
  /** Agility and precision: attack speed, dodge, ranged damage, stealth. */
  DEXTERITY = 1,
  /** Health and resilience: max HP, HP regen, poison resistance. */
  CONSTITUTION = 2,
  /** Mental acuity: magic damage, mana pool, spell unlock. */
  INTELLIGENCE = 3,
  /** Intuition and insight: mana regen, spell cost, healing power. */
  WISDOM = 4,
  /** Social influence: trading prices, NPC disposition, pet power. */
  CHARISMA = 5,
  /** Fortune: critical hits, loot quality, random event outcomes. */
  LUCK = 6,
  /** Awareness: trap detection, stealth piercing, ranged accuracy. */
  PERCEPTION = 7,
}

export const ATTRIBUTE_NAMES: Record<Attribute, string> = {
  [Attribute.STRENGTH]: "Strength",
  [Attribute.DEXTERITY]: "Dexterity",
  [Attribute.CONSTITUTION]: "Constitution",
  [Attribute.INTELLIGENCE]: "Intelligence",
  [Attribute.WISDOM]: "Wisdom",
  [Attribute.CHARISMA]: "Charisma",
  [Attribute.LUCK]: "Luck",
  [Attribute.PERCEPTION]: "Perception",
};

export const ATTRIBUTE_DESCRIPTIONS: Record<Attribute, string> = {
  [Attribute.STRENGTH]: "Raw physical power. Determines melee damage, carry capacity, and the force of blows.",
  [Attribute.DEXTERITY]: "Agility, reflexes, and balance. Governs attack speed, dodge chance, and ranged precision.",
  [Attribute.CONSTITUTION]: "Vitality and endurance. Increases health pool, recovery rates, and resistance to toxins.",
  [Attribute.INTELLIGENCE]: "Mental power and memory. Amplifies magic damage, expands mana reserves, and unlocks higher spells.",
  [Attribute.WISDOM]: "Spiritual insight and clarity. Enhances mana regeneration, reduces spell costs, and strengthens healing.",
  [Attribute.CHARISMA]: "Force of personality. Lowers trading prices, improves faction reputation gains, and empowers companions.",
  [Attribute.LUCK]: "Fortune's favor. Improves critical hit rate, loot quality, salvage yields, and tilts random outcomes.",
  [Attribute.PERCEPTION]: "Awareness of the world. Detects hidden traps and enemies, improves ranged accuracy, and reveals secrets.",
};
```

### 2.2 Attribute Ranges & Scaling

```
Rating      Range      Description
─────────────────────────────────────────────
Terrible    0-4       Crippled or extremely deficient
Poor        5-9       Below average
Average    10-14      Typical human adult
Good       15-19      Above average
Excellent  20-24      Trained professional
Superior   25-29      Elite specialist
Peerless   30-39      Legendary hero
Transcendent 40-49    Demigod
Mythic      50+       Godlike
```

The maximum useful attribute value is 100. Beyond 100, all formulas use diminishing returns.

### 2.3 Diminishing Returns

```typescript
// /src/content/stats/DiminishedValue.ts

/**
 * Calculate the effective value of an attribute beyond the soft cap.
 *
 * Formula:
 *   If value <= softCap: return value
 *   If value > softCap:
 *     effective = softCap + (value - softCap) * (softCap / (value - softCap + softCap))
 *
 * This produces a smooth asymptotic curve:
 *   - At softCap+20, effective ≈ softCap + 10
 *   - At softCap+50, effective ≈ softCap + 17
 *   - At softCap+100, effective ≈ softCap + 20
 *   - Never exceeds softCap * 2
 *
 * @param rawValue The raw attribute value (0-255)
 * @param softCap The threshold where diminishing returns begin (default 30)
 * @returns The effective value after diminishing returns
 */
export function diminish(rawValue: number, softCap: number = 30): number {
  if (rawValue <= softCap) return rawValue;
  return softCap + (rawValue - softCap) * (softCap / (rawValue - softCap + softCap));
}
```

### 2.4 Species Attribute Ranges

| Species | STR | DEX | CON | INT | WIS | CHA | LUK | PER |
|:--------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Human | 6-12 | 6-12 | 6-12 | 6-12 | 6-12 | 6-12 | 5-10 | 6-12 |
| Elf | 4-8 | 10-16 | 4-8 | 8-14 | 10-16 | 8-12 | 5-10 | 10-16 |
| Dwarf | 10-16 | 4-8 | 12-18 | 6-10 | 6-10 | 4-8 | 5-10 | 6-10 |
| Orc | 14-20 | 6-10 | 12-18 | 3-7 | 3-7 | 3-7 | 3-8 | 4-8 |
| Halfling | 3-7 | 10-16 | 6-12 | 6-12 | 8-14 | 10-16 | 10-18 | 10-16 |
| Beastkin | 8-16 | 10-16 | 8-14 | 4-10 | 6-10 | 4-10 | 4-8 | 12-18 |

### 2.5 Class Attribute Growth (Per Level)

| Class | STR | DEX | CON | INT | WIS | CHA | LUK | PER |
|:------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Warrior | 3.0 | 1.5 | 2.5 | 0.5 | 1.0 | 0.5 | 0.5 | 1.0 |
| Mage | 0.5 | 1.0 | 1.0 | 3.0 | 2.5 | 1.0 | 0.5 | 1.5 |
| Rogue | 1.5 | 3.0 | 1.5 | 1.0 | 0.5 | 1.5 | 1.5 | 2.5 |
| Cleric | 1.5 | 0.5 | 2.0 | 1.5 | 3.0 | 1.5 | 1.0 | 1.0 |
| Ranger | 1.0 | 2.5 | 2.0 | 0.5 | 2.5 | 1.0 | 1.0 | 2.5 |
| Paladin | 2.5 | 1.0 | 2.5 | 0.5 | 2.0 | 1.5 | 0.5 | 1.0 |
| Necromancer | 0.5 | 1.0 | 1.5 | 3.0 | 2.0 | 0.5 | 0.5 | 1.0 |
| Bard | 0.5 | 2.0 | 1.0 | 1.5 | 1.5 | 3.0 | 2.0 | 1.5 |

---

## 3. Secondary Derived Stats

Derived stats are computed from primary attributes, equipment modifiers, and active buffs. They are never stored directly — they are recomputed whenever a primary attribute, piece of equipment, or buff changes.

### 3.1 Combat Stats

```typescript
// /src/content/stats/CombatStats.ts

import type { Attribute } from "./PrimaryAttributes.js";
import { diminish } from "./DiminishedValue.js";

export interface CombatDerivedStats {
  /** Base melee damage before weapon modifiers. */
  readonly meleeDamage: number;
  /** Base ranged damage before bow modifiers. */
  readonly rangedDamage: number;
  /** Base magic damage before spell power modifiers. */
  readonly magicDamage: number;
  /** Attack speed multiplier (1.0 = normal). */
  readonly attackSpeed: number;
  /** Ranged attack speed multiplier. */
  readonly rangedAttackSpeed: number;
  /** Cast speed multiplier (1.0 = normal). */
  readonly castSpeed: number;
  /** Chance to critically hit (0.0-1.0). */
  readonly criticalChance: number;
  /** Critical damage multiplier (2.0 = double damage). */
  readonly criticalDamage: number;
  /** Armor penetration — flat reduction of target armor (0-100). */
  readonly armorPenetration: number;
  /** Bonus damage to specific enemy types (e.g., +5 vs undead). */
  readonly bonusDamage: Record<string, number>;
}

/**
 * Compute all combat-derived stats from primary attributes.
 *
 * STR → meleeDamage:  every 10 STR = +2 base melee damage
 * DEX → attackSpeed: every 10 DEX = +5% attack speed (additive)
 * INT → magicDamage: every 10 INT = +2 base magic damage
 * DEX → criticalChance: every 10 DEX = +1% crit (diminishing after 50)
 * LUK → criticalChance: every 10 LUK = +1.5% crit (diminishing after 30)
 * LUK → criticalDamage: every 10 LUK = +5% crit damage
 */
export function computeCombatStats(attrs: AttributeSet, level: number): CombatDerivedStats {
  const str = diminish(attrs[Attribute.STRENGTH]);
  const dex = diminish(attrs[Attribute.DEXTERITY]);
  const int = diminish(attrs[Attribute.INTELLIGENCE]);
  const luk = diminish(attrs[Attribute.LUCK]);
  const per = diminish(attrs[Attribute.PERCEPTION]);

  return {
    meleeDamage: Math.floor(str * 0.2) + Math.floor(level * 0.5),
    rangedDamage: Math.floor(dex * 0.15) + Math.floor(per * 0.1) + Math.floor(level * 0.3),
    magicDamage: Math.floor(int * 0.2) + Math.floor(level * 0.5),

    attackSpeed: 1.0 + dex * 0.005,
    rangedAttackSpeed: 1.0 + dex * 0.004 + per * 0.002,
    castSpeed: 1.0 + dex * 0.003 + int * 0.003,

    criticalChance: Math.min(0.75, dex * 0.001 + luk * 0.0015),
    criticalDamage: 2.0 + luk * 0.005,

    armorPenetration: Math.floor(str * 0.2),

    bonusDamage: {},
  };
}
```

### 3.2 Defense Stats

```typescript
// /src/content/stats/DefenseStats.ts

import { diminish } from "./DiminishedValue.js";

export interface DefenseDerivedStats {
  /** Maximum health points. */
  readonly maxHp: number;
  /** Base physical armor (damage reduction). */
  readonly armor: number;
  /** Damage reduction from armor (0.0-0.8). Formula: armor / (armor + 100). */
  readonly damageReduction: number;
  /** Toughness reduces incoming damage by a flat amount. */
  readonly toughness: number;
  /** Chance to dodge an incoming attack (0.0-0.5). */
  readonly dodgeChance: number;
  /** Chance to block with shield (0.0-0.6). */
  readonly blockChance: number;
  /** Flat damage reduction from blocking. */
  readonly blockValue: number;
  /** Resistance to fire, ice, lightning, poison, dark, holy (0-100 each). */
  readonly resistances: Record<string, number>;
}

/**
 * Compute all defense-derived stats.
 *
 * CON → maxHp:       every 1 CON = +10 max HP
 * DEX → dodgeChance: every 10 DEX = +0.5% dodge (diminishing after 60 DEX)
 * CON → resistances: every 10 CON = +1 all resistances
 *
 * Armor formula (diminishing):
 *   damageReduction = armor / (armor + 100)
 *   At 50 armor → 33% reduction
 *   At 100 armor → 50% reduction
 *   At 200 armor → 66% reduction
 */
export function computeDefenseStats(
  attrs: AttributeSet,
  level: number,
  equipmentArmor: number,
  equipmentToughness: number,
): DefenseDerivedStats {
  const con = diminish(attrs[Attribute.CONSTITUTION]);
  const dex = diminish(attrs[Attribute.DEXTERITY]);

  const baseArmor = equipmentArmor + Math.floor(con * 0.5);
  const totalArmor = baseArmor;

  return {
    maxHp: 100 + Math.floor(con * 10) + Math.floor(level * 5),
    armor: totalArmor,
    damageReduction: totalArmor / (totalArmor + 100),
    toughness: equipmentToughness,
    dodgeChance: Math.min(0.5, dex * 0.0005),
    blockChance: 0, // granted by shields
    blockValue: 0,  // granted by shields
    resistances: {
      fire: Math.floor(con * 0.1),
      ice: Math.floor(con * 0.1),
      lightning: Math.floor(con * 0.1),
      poison: Math.floor(con * 0.2),
      dark: Math.floor(con * 0.1),
      holy: Math.floor(con * 0.1),
    },
  };
}
```

### 3.3 Magic Stats

```typescript
// /src/content/stats/MagicStats.ts

import { diminish } from "./DiminishedValue.js";

export interface MagicDerivedStats {
  /** Maximum mana pool. */
  readonly maxMana: number;
  /** Mana regenerated per second (base). */
  readonly manaRegen: number;
  /** Mana regenerated per second while out of combat. */
  readonly manaRegenOutOfCombat: number;
  /** Spell power multiplier. */
  readonly spellPower: number;
  /** Spell cost reduction (0.0-0.8, 80% max reduction). */
  readonly spellCostReduction: number;
  /** Cooldown reduction (0.0-0.6, 60% max). */
  readonly cooldownReduction: number;
  /** Healing power multiplier. */
  readonly healingPower: number;
  /** Maximum number of simultaneous summoned creatures. */
  readonly maxSummons: number;
  /** Mana restored per second while channeling. */
  readonly manaChanneling: number;
}

/**
 * Compute all magic-derived stats.
 *
 * INT → maxMana:       every 1 INT = +5 max mana
 * INT → spellPower:    every 10 INT = +3% spell power
 * WIS → manaRegen:     every 1 WIS = +0.1 mana/sec
 * WIS → spellCost:     every 1 WIS = -0.5% spell cost (max 80%)
 * WIS → healingPower:  every 10 WIS = +5% healing
 * INT → maxSummons:    every 15 INT = +1 max summon
 */
export function computeMagicStats(attrs: AttributeSet, level: number): MagicDerivedStats {
  const int = diminish(attrs[Attribute.INTELLIGENCE]);
  const wis = diminish(attrs[Attribute.WISDOM]);

  const baseMana = int * 5;

  return {
    maxMana: baseMana + level * 3,
    manaRegen: Math.max(0.5, wis * 0.1 + level * 0.05),
    manaRegenOutOfCombat: Math.max(1.0, wis * 0.2 + level * 0.1),
    spellPower: 1.0 + int * 0.003,
    spellCostReduction: Math.min(0.8, wis * 0.005),
    cooldownReduction: Math.min(0.6, wis * 0.002 + int * 0.001),
    healingPower: 1.0 + wis * 0.005,
    maxSummons: 1 + Math.floor(int / 15),
    manaChanneling: Math.max(1.0, wis * 0.05 + level * 0.02),
  };
}
```

### 3.4 Recovery Stats

```typescript
// /src/content/stats/RecoveryStats.ts

import { diminish } from "./DiminishedValue.js";

export interface RecoveryDerivedStats {
  /** HP regenerated per second (base). */
  readonly hpRegen: number;
  /** HP regenerated per second while out of combat. */
  readonly hpRegenOutOfCombat: number;
  /** Stamina regenerated per second. */
  readonly staminaRegen: number;
  /** Healing received multiplier (reduces incoming healing if negative). */
  readonly healingReceived: number;
  /** Bonus HP regen while sitting/resting. */
  readonly restBonus: number;
  /** Time before out-of-combat regen kicks in (seconds). */
  readonly combatCooldown: number;
}

/**
 * Compute recovery stats.
 *
 * CON → hpRegen:       every 10 CON = +0.5 HP/sec base
 * CON → hpRegenOoC:   every 10 CON = +2 HP/sec out of combat
 * DEX → staminaRegen:  every 10 DEX = +0.3 stamina/sec
 */
export function computeRecoveryStats(attrs: AttributeSet): RecoveryDerivedStats {
  const con = diminish(attrs[Attribute.CONSTITUTION]);
  const dex = diminish(attrs[Attribute.DEXTERITY]);

  return {
    hpRegen: Math.max(0.1, con * 0.05),
    hpRegenOutOfCombat: Math.max(0.5, con * 0.2),
    staminaRegen: Math.max(0.5, dex * 0.03 + con * 0.02),
    healingReceived: 1.0, // modified by effects
    restBonus: Math.floor(con * 0.1),
    combatCooldown: Math.max(3, 10 - Math.floor(con * 0.05)),
  };
}
```

### 3.5 Utility Stats

```typescript
// /src/content/stats/UtilityStats.ts

import { diminish } from "./DiminishedValue.js";

export interface UtilityDerivedStats {
  /** Maximum carry weight in kilograms. */
  readonly carryWeight: number;
  /** Movement speed in blocks/second. */
  readonly moveSpeed: number;
  /** Jump height in blocks (1.0 = normal). */
  readonly jumpHeight: number;
  /** Stealth rating (higher = harder to detect). */
  readonly stealthRating: number;
  /** Detection range multiplier (higher PER = see farther). */
  readonly detectionRange: number;
  /** Mining speed multiplier. */
  readonly miningSpeed: number;
  /** Fall damage reduction (0.0-1.0). */
  readonly fallDamageReduction: number;
}

/**
 * Compute utility stats.
 *
 * STR → carryWeight:     every 1 STR = +5 kg carry weight (base 100)
 * DEX → moveSpeed:       every 10 DEX = +1% move speed
 * DEX → stealthRating:   every 10 DEX = +2 stealth
 * PER → detectionRange:  every 10 PER = +10% detection range
 * STR → miningSpeed:     every 10 STR = +5% mining speed
 * DEX → fallDamageReduction: every 10 DEX = -3% fall damage
 */
export function computeUtilityStats(attrs: AttributeSet, baseSpeed: number): UtilityDerivedStats {
  const str = diminish(attrs[Attribute.STRENGTH]);
  const dex = diminish(attrs[Attribute.DEXTERITY]);
  const per = diminish(attrs[Attribute.PERCEPTION]);

  return {
    carryWeight: 100 + str * 5,
    moveSpeed: baseSpeed * (1.0 + dex * 0.001),
    jumpHeight: 1.0 + dex * 0.002,
    stealthRating: Math.floor(dex * 0.2),
    detectionRange: 1.0 + per * 0.01,
    miningSpeed: 1.0 + str * 0.005,
    fallDamageReduction: Math.min(0.8, dex * 0.003),
  };
}
```

### 3.6 Luck Stats

```typescript
// /src/content/stats/LuckStats.ts

import { diminish } from "./DiminishedValue.js";

export interface LuckDerivedStats {
  /** Loot quality multiplier (>1 = better loot, <1 = worse). */
  readonly lootQuality: number;
  /** Bonus loot quantity rolls. */
  readonly bonusLootRolls: number;
  /** Salvage yield multiplier. */
  readonly salvageYield: number;
  /** Chance to find rare items (0.0-1.0). */
  readonly findRarity: number;
  /** Trap avoidance chance (0.0-1.0). */
  readonly trapAvoidance: number;
  /** Price bonus when selling (1.0 = normal). */
  readonly sellPriceBonus: number;
  /** Price discount when buying (1.0 = normal). */
  readonly buyPriceDiscount: number;
  /** Minigame bonus (lockpicking, fishing, gambling). */
  readonly minigameBonus: number;
  /** Misc event tilt: random encounters favor positive outcomes. */
  readonly eventTilt: number;
}

/**
 * Compute luck-derived stats.
 *
 * LUK → lootQuality:      every 10 LUK = +5% loot quality
 * LUK → bonusLootRolls:   every 20 LUK = +1 bonus roll
 * LUK → salvageYield:     every 10 LUK = +3% salvage yield
 * LUK → trapAvoidance:    every 10 LUK = +2% trap avoidance (max 60%)
 * LUK → eventTilt:        every 10 LUK = +5% positive event bias
 * CHA → sellPriceBonus:   every 10 CHA = +3% sell price
 * CHA → buyPriceDiscount: every 10 CHA = -2% buy price (max -40%)
 */
export function computeLuckStats(attrs: AttributeSet): LuckDerivedStats {
  const luk = diminish(attrs[Attribute.LUCK]);
  const cha = diminish(attrs[Attribute.CHARISMA]);

  return {
    lootQuality: 1.0 + luk * 0.005,
    bonusLootRolls: Math.floor(luk / 20),
    salvageYield: 1.0 + luk * 0.003,
    findRarity: Math.min(0.3, luk * 0.002),
    trapAvoidance: Math.min(0.6, luk * 0.002),
    sellPriceBonus: 1.0 + cha * 0.003,
    buyPriceDiscount: Math.max(0.6, 1.0 - cha * 0.002),
    minigameBonus: Math.floor(luk * 0.1 + cha * 0.05),
    eventTilt: Math.min(0.8, luk * 0.005),
  };
}
```

---

## 4. Complete Stat Computation Pipeline

```typescript
// /src/content/stats/StatFormulas.ts

/**
 * Master function that computes ALL derived stats from primary attributes.
 * This is called whenever:
 *   - Character levels up (attributes change)
 *   - Equipment changes (modifiers added/removed)
 *   - Buff/debuff is applied or expires
 *   - A new character is created
 *
 * Complexity: O(1) — flat computations, no loops or allocations.
 *
 * @param attrs      Raw primary attribute values (0-255)
 * @param level      Character level (1-999)
 * @param equipment  Summed equipment modifiers
 * @param buffs      Active buff/debuff modifiers (see §5)
 * @param species    Species base speed and size
 * @returns All derived stats packed into a single struct
 */
export function computeAllStats(
  attrs: AttributeSet,
  level: number,
  equipment: EquipmentModifiers,
  buffs: BuffModifiers,
  species: SpeciesCharacteristics,
): CompleteDerivedStats {
  // 1. Apply diminishing returns to all attributes
  const effectiveAttrs = applyDiminishToAll(attrs);

  // 2. Compute each category
  const combat = computeCombatStats(effectiveAttrs, level);
  const defense = computeDefenseStats(effectiveAttrs, level, equipment.armor, equipment.toughness);
  const magic = computeMagicStats(effectiveAttrs, level);
  const recovery = computeRecoveryStats(effectiveAttrs);
  const utility = computeUtilityStats(effectiveAttrs, species.baseSpeed);
  const luck = computeLuckStats(effectiveAttrs);

  // 3. Apply equipment modifiers (flat additions)
  applyEquipmentModifiers(combat, defense, magic, recovery, utility, luck, equipment);

  // 4. Apply buff/debuff modifiers (typed stacking)
  applyBuffModifiers(combat, defense, magic, recovery, utility, luck, buffs);

  // 5. Clamp all values to valid ranges
  clampAll(combat, defense, magic, recovery, utility, luck);

  return {
    ...combat,
    ...defense,
    ...magic,
    ...recovery,
    ...utility,
    ...luck,
  };
}

export interface CompleteDerivedStats extends
  CombatDerivedStats,
  DefenseDerivedStats,
  MagicDerivedStats,
  RecoveryDerivedStats,
  UtilityDerivedStats,
  LuckDerivedStats {}

/**
 * Convenience: compute stats directly from a CharacterComponent row.
 */
export function computeStatsFromCharacter(
  charRow: number,
  charData: CharacterComponentData,
  level: number,
): CompleteDerivedStats {
  const attrs: AttributeSet = [
    charData.baseStrength[charRow],
    charData.baseDexterity[charRow],
    charData.baseVitality[charRow],   // CON
    charData.baseIntelligence[charRow],
    charData.baseWisdom[charRow],
    charData.baseCharisma[charRow],
    charData.baseLuck[charRow],
    charData.basePerception[charRow],
  ];
  return computeAllStats(attrs, level, EMPTY_EQUIPMENT, EMPTY_BUFFS, DEFAULT_SPECIES);
}
```

---

## 5. Modifier Stacking Rules

### 5.1 Modifier Types

```
Modifier Source      Stacking Rule       Example
──────────────────────────────────────────────────────────
Equipment           Additive (sum)      Two +5 STR rings = +10 STR
Buff (same spell)   Highest only        2× Bless = only strongest applies
Buff (different)    Multiplicative       Bless(+10%) × War Cry(+15%) = +26.5%
Debuff              Highest only        2× Weakness = only strongest applies
Enchantment         Additive            +3 fire damage + +2 fire damage = +5
Passive Perk        Additive            2 ranks of Brawn = +10% melee
Set Bonus           Flat                Full armor set = +20 armor
Potion              Highest only        2× Health potions don't stack
Food                Highest only        Best food buff applies
Environment         Multiplicative       Underwater = speed × 0.5
```

### 5.2 Modifier Application

```typescript
// /src/content/stats/ModifierStack.ts

export const enum ModifierType {
  FLAT,         // Added directly: result = base + value
  PERCENT,      // Percentage: result = base × (1 + value)
  MULTIPLY,     // Multiplicative: result = base × value
  HIGHEST,      // Source-grouped: only the highest value in this group applies
}

export interface StatModifier {
  readonly stat: string;        // e.g., "meleeDamage", "maxMana", "moveSpeed"
  readonly type: ModifierType;
  readonly value: number;
  readonly sourceGroup: string; // e.g., "bless", "equipment_ring", "potion_heal"
  readonly duration?: number;   // seconds remaining (0 = permanent/passive)
}

/**
 * Apply a list of modifiers to a base stat value.
 *
 * @param base      The base value before modifiers
 * @param modifiers Array of active modifiers
 * @returns The final value after all modifiers
 */
export function applyModifiers(base: number, modifiers: StatModifier[]): number {
  // Group by source for HIGHEST type
  const highestGroups = new Map<string, number>();
  const regularMods: StatModifier[] = [];

  for (const mod of modifiers) {
    if (mod.type === ModifierType.HIGHEST) {
      const existing = highestGroups.get(mod.sourceGroup) ?? -Infinity;
      highestGroups.set(mod.sourceGroup, Math.max(existing, mod.value));
    } else {
      regularMods.push(mod);
    }
  }

  // Apply HIGHEST modifiers (one per group)
  for (const value of highestGroups.values()) {
    if (value > 0) regularMods.push({ stat: "", type: ModifierType.FLAT, value, sourceGroup: "" });
  }

  // Apply FLAT modifiers first (summed)
  let result = base;
  for (const mod of regularMods) {
    if (mod.type === ModifierType.FLAT) result += mod.value;
  }

  // Apply PERCENT modifiers (summed, then applied as multiplier)
  let percentSum = 0;
  for (const mod of regularMods) {
    if (mod.type === ModifierType.PERCENT) percentSum += mod.value;
  }
  if (percentSum !== 0) result *= 1 + percentSum;

  // Apply MULTIPLY modifiers (each applied sequentially)
  for (const mod of regularMods) {
    if (mod.type === ModifierType.MULTIPLY) result *= mod.value;
  }

  return result;
}
```

### 5.3 Temporary Stat Changes

Temporary stat changes (buffs, debuffs, potions, food, environment) are stored in a `StatModifierComponent` attached to the character entity:

```typescript
// /src/engine/ecs/components/StatModifierComponent.ts

/**
 * Stores active temporary modifiers on a character.
 * This component is iterated by the ModifierSystem each tick
 * to decrement durations and remove expired modifiers.
 */
export const StatModifierDesc = {
  modifierCount: { type: Uint8Array, length: 1 },
  // Up to 16 simultaneous modifiers per character
  modStat0:  { type: Uint16Array, length: 1 }, // stat enum
  modType0:  { type: Uint8Array,  length: 1 }, // ModifierType
  modValue0: { type: Float32Array, length: 1 },
  modGroup0: { type: Uint16Array, length: 1 }, // source group enum
  modTime0:  { type: Float32Array, length: 1 }, // remaining seconds
  // ... repeats for 1-15
} as const satisfies ComponentDesc;
```

---

## 6. Skill System Integration

Each skill is governed by 1-2 primary attributes. The governing stat determines the maximum effective skill level and the XP gain rate.

```typescript
// /src/content/skills/SkillGoverning.ts

/**
 * Each skill is governed by one or two primary attributes.
 *
 * Skill formula:
 *   effectiveLevel = min(skillLevel, average(governingStats) * 3)
 *   xpGainRate = 1 + average(governingStats) * 0.02
 *
 * This means a character with 50 INT can never exceed 150 effective
 * Arcane Magic skill, no matter how much they train. This creates
 * natural class specialization.
 */
export const SKILL_GOVERNING: Record<number, [Attribute, Attribute?]> = {
  // Combat skills
  1:  [Attribute.STRENGTH, Attribute.DEXTERITY],  // One-Handed
  2:  [Attribute.STRENGTH, Attribute.CONSTITUTION], // Two-Handed
  3:  [Attribute.DEXTERITY, Attribute.PERCEPTION], // Archery
  4:  [Attribute.STRENGTH, Attribute.CONSTITUTION], // Blocking
  5:  [Attribute.STRENGTH, Attribute.DEXTERITY],  // Unarmed

  // Magic skills
  10: [Attribute.INTELLIGENCE],                    // Arcane Magic
  11: [Attribute.INTELLIGENCE, Attribute.WISDOM],  // Fire Magic
  12: [Attribute.INTELLIGENCE, Attribute.WISDOM],  // Ice Magic
  13: [Attribute.WISDOM, Attribute.CHARISMA],      // Holy Magic
  14: [Attribute.INTELLIGENCE, Attribute.LUCK],    // Dark Magic
  15: [Attribute.WISDOM, Attribute.PERCEPTION],    // Nature Magic

  // Crafting skills
  20: [Attribute.STRENGTH, Attribute.DEXTERITY],  // Smithing
  21: [Attribute.DEXTERITY, Attribute.PERCEPTION], // Woodworking
  22: [Attribute.INTELLIGENCE, Attribute.WISDOM],  // Alchemy
  23: [Attribute.INTELLIGENCE, Attribute.LUCK],    // Enchanting
  24: [Attribute.WISDOM, Attribute.CHARISMA],      // Cooking

  // Gathering skills
  30: [Attribute.STRENGTH, Attribute.CONSTITUTION], // Mining
  31: [Attribute.WISDOM, Attribute.PERCEPTION],    // Foraging
  32: [Attribute.DEXTERITY, Attribute.PERCEPTION], // Hunting
  33: [Attribute.DEXTERITY, Attribute.LUCK],       // Fishing

  // General skills
  40: [Attribute.DEXTERITY, Attribute.LUCK],       // Stealth
  41: [Attribute.DEXTERITY, Attribute.PERCEPTION], // Lockpicking
  42: [Attribute.CHARISMA, Attribute.WISDOM],      // Persuasion
  43: [Attribute.CHARISMA, Attribute.LUCK],        // Trading
};
```

---

## 7. Damage Formula

```typescript
// /src/content/stats/DamageFormula.ts

/**
 * Complete damage calculation for a physical attack.
 *
 * Formula:
 *   baseDamage = weaponDamage + meleeDamageBonus
 *   critCheck = random(0, 1) < criticalChance
 *   critMultiplier = critCheck ? criticalDamage : 1.0
 *   armorMitigation = target.armor / (target.armor + 100)
 *   finalDamage = (baseDamage * critMultiplier * skillMultiplier) *
 *                 (1 - armorMitigation) - target.toughness
 *
 * Minimum damage: 1 (toughness cannot reduce to 0). 
 * Maximum damage: capped at 10× base (prevents absurd crits).
 */
export function calculatePhysicalDamage(
  attacker: CombatDerivedStats,
  defender: DefenseDerivedStats,
  weaponDamage: number,
  skillMultiplier: number,
  rng: () => number,
): { damage: number; isCrit: boolean; isDodged: boolean } {
  // Dodge check
  if (rng() < defender.dodgeChance) {
    return { damage: 0, isCrit: false, isDodged: true };
  }

  const baseDamage = weaponDamage + attacker.meleeDamage;
  const isCrit = rng() < attacker.criticalChance;
  const critMult = isCrit ? attacker.criticalDamage : 1.0;
  const armorMult = 1 - defender.damageReduction;
  const rawDamage = baseDamage * critMult * skillMultiplier * armorMult;
  const finalDamage = Math.max(1, Math.floor(rawDamage - defender.toughness));

  return {
    damage: Math.min(finalDamage, baseDamage * 10), // cap absurd crits
    isCrit,
    isDodged: false,
  };
}

/**
 * Magic damage uses a different formula — it ignores armor but
 * is reduced by magic resistance.
 *
 * Formula:
 *   baseDamage = spellBaseDamage + magicDamageBonus
 *   resistanceMitigation = 1 - (target.resistances[element] / 100)
 *   finalDamage = baseDamage * spellPower * resistanceMitigation
 */
export function calculateMagicDamage(
  attacker: MagicDerivedStats,
  spellBaseDamage: number,
  spellPowerMultiplier: number,
  targetResistance: number, // 0-100
): number {
  const resistanceMitigation = 1 - targetResistance / 100;
  const raw = (spellBaseDamage + attacker.magicDamage) *
              attacker.spellPower * spellPowerMultiplier * resistanceMitigation;
  return Math.max(1, Math.floor(raw));
}
```

---

## 8. Stat UI Display

Stats are displayed in the character sheet with color coding and tooltips:

```
CHARACTER STATS
═══════════════════════════════════════
Primary Attributes:
  Strength:      18  (Good)
  Dexterity:     22  (Excellent)  ←
  Constitution:  14  (Average)
  Intelligence:   8  (Poor)
  Wisdom:        12  (Average)
  Charisma:      15  (Good)
  Luck:          16  (Good)
  Perception:    11  (Average)

Combat:
  Melee Damage:   12
  Attack Speed:  1.12×
  Crit Chance:   5.2%
  Crit Damage:   2.08×

Defense:
  Max HP:        240
  Armor:         78  (43.8% reduction)
  Dodge:         1.1%

Magic:
  Max Mana:       40
  Mana Regen:    1.8/s
  Spell Power:   1.024×

Utility:
  Carry Weight:  190 kg
  Move Speed:   4.5 blocks/s
  Stealth:        4

Luck:
  Loot Quality:  1.08×
  Trap Avoid:    3.2%
```

---

## 9. Complete Stat Formula Reference

### 9.1 Primary → Secondary Formula Table

| Derived Stat | Formula | Soft Cap |
|:-------------|:--------|:--------:|
| **meleeDamage** | `floor(STR × 0.2 + level × 0.5)` | STR 30 |
| **rangedDamage** | `floor(DEX × 0.15 + PER × 0.1 + level × 0.3)` | DEX 30 |
| **magicDamage** | `floor(INT × 0.2 + level × 0.5)` | INT 30 |
| **attackSpeed** | `1.0 + DEX × 0.005` | none |
| **rangedAttackSpeed** | `1.0 + DEX × 0.004 + PER × 0.002` | none |
| **castSpeed** | `1.0 + DEX × 0.003 + INT × 0.003` | none |
| **criticalChance** | `min(0.75, DEX × 0.001 + LUK × 0.0015)` | DEX 50, LUK 30 |
| **criticalDamage** | `2.0 + LUK × 0.005` | LUK 30 |
| **armorPenetration** | `floor(STR × 0.2)` | STR 30 |
| **maxHp** | `100 + floor(CON × 10 + level × 5)` | none |
| **armor** | `gear + floor(CON × 0.5)` | none |
| **damageReduction** | `armor / (armor + 100)` | — |
| **dodgeChance** | `min(0.5, DEX × 0.0005)` | DEX 60 |
| **maxMana** | `INT × 5 + level × 3` | none |
| **manaRegen** | `max(0.5, WIS × 0.1 + level × 0.05)` | WIS 40 |
| **manaRegenOoC** | `max(1.0, WIS × 0.2 + level × 0.1)` | WIS 40 |
| **spellPower** | `1.0 + INT × 0.003` | INT 40 |
| **spellCostReduction** | `min(0.8, WIS × 0.005)` | WIS 50 |
| **healingPower** | `1.0 + WIS × 0.005` | WIS 40 |
| **maxSummons** | `1 + floor(INT / 15)` | INT 60 |
| **hpRegen** | `max(0.1, CON × 0.05)` | CON 40 |
| **hpRegenOoC** | `max(0.5, CON × 0.2)` | CON 40 |
| **staminaRegen** | `max(0.5, DEX × 0.03 + CON × 0.02)` | DEX 40 |
| **carryWeight** | `100 + STR × 5` | none |
| **moveSpeed** | `baseSpeed × (1.0 + DEX × 0.001)` | DEX 50 |
| **jumpHeight** | `1.0 + DEX × 0.002` | DEX 50 |
| **stealthRating** | `floor(DEX × 0.2)` | DEX 40 |
| **detectionRange** | `1.0 + PER × 0.01` | PER 40 |
| **miningSpeed** | `1.0 + STR × 0.005` | STR 50 |
| **lootQuality** | `1.0 + LUK × 0.005` | LUK 30 |
| **bonusLootRolls** | `floor(LUK / 20)` | LUK 40 |
| **salvageYield** | `1.0 + LUK × 0.003` | LUK 30 |
| **trapAvoidance** | `min(0.6, LUK × 0.002)` | LUK 40 |
| **sellPriceBonus** | `1.0 + CHA × 0.003` | CHA 40 |
| **buyPriceDiscount** | `max(0.6, 1.0 - CHA × 0.002)` | CHA 50 |
| **minigameBonus** | `floor(LUK × 0.1 + CHA × 0.05)` | LUK 30 |
| **eventTilt** | `min(0.8, LUK × 0.005)` | LUK 40 |

### 9.2 XP & Leveling Formula

```
XP for level N = floor(N ^ 1.8 + N × 50)

Total XP for level N = sum_{i=1}^{N-1} floor(i ^ 1.8 + i × 50)

Level mapping:
  Level  1:      0 XP (start)
  Level  2:    152 XP
  Level  5:    418 XP
  Level 10:  1,131 XP
  Level 15:  2,234 XP
  Level 20:  4,347 XP
  Level 30: 11,269 XP
  Level 50: 36,274 XP
  Level 75: 91,882 XP
  Level 100: 208,509 XP
```

### 9.3 Elemental Resistance Formula

```
damage = rawDamage × (1 - resistance / 100)

Resistance     Mitigation
    0            0%
   25           25%
   50           50%
   75           75%
  100          100% (immunity)

Resistance above 100 is converted to healing:
  damage = -rawDamage × ((resistance - 100) / 100)
  
Example: Fire Resistance 120 → fire attacks heal for 20% of their damage.
```

---

## 10. Summary of New Files

```
src/content/stats/
├── PrimaryAttributes.ts      — Attribute enum, names, descriptions, ranges
├── DiminishedValue.ts        — Diminishing returns formula
├── CombatStats.ts            — Melee/ranged/magic damage, crit, attack speed
├── DefenseStats.ts           — Max HP, armor, dodge, block, resistances
├── MagicStats.ts             — Mana, regen, spell power, cooldown reduction
├── RecoveryStats.ts          — HP/Mana/Stamina regen, rest bonus
├── UtilityStats.ts           — Carry weight, move speed, stealth, mining
├── LuckStats.ts              — Loot quality, salvage, trap avoidance, event tilt
├── StatFormulas.ts           — Master computeAllStats pipeline
├── ModifierStack.ts          — StatModifier type, applyModifiers, stacking rules
└── DamageFormula.ts          — Physical and magical damage calculation

src/engine/ecs/components/
└── StatModifierComponent.ts  — Temporary buff/debuff storage on character entities

src/content/skills/
└── SkillGoverning.ts         — Maps skills to governing attributes
```

---

## 11. Attribute Computation Summary

```
Species Base Ranges
  ↓ (pick midpoint + statModifiers)
Species-Adjusted Base
  ↓ (+ class base stats × 0.6 + species base × 0.4)
Class-Composited Stats
  ↓ (+ background flat bonuses)
Background-Adjusted Stats
  ↓ (+ per-level growth from class growth rates)
Level-Scaled Stats
  ↓ (apply diminishing returns)
Effective Primary Attributes (8)
  ↓ (compute derived formulas)
Raw Derived Stats (20+)
  ↓ (apply equipment: flat additions)
Equipment-Modified Stats
  ↓ (apply buffs: typed stacking rules)
Final Derived Stats
  ↓ (used by all gameplay systems)
In-Game Behavior
```
