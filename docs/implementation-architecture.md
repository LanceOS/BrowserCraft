# Implementation Architecture — Clean Code, Design Patterns & Performance

**Version:** 2.0 (C++ Port)  
**Scope:** How to implement the engine, items, flora, stats, character, and transformation systems with clean, decoupled, maintainable, and high-performance C++ code. Design patterns, dependency injection, composition root, avoiding technical debt, profiling-guided optimization.

---

## 1. Guiding Principles

### 1.1 Clean Code Tenets

1. **Separate policy from mechanism.** Systems decide *what* to do; data determines *how*. A `CropGrowthSystem` tells a crop to grow; the crop's `FloraProperties` define its growth rate, light needs, and soil affinity.
2. **One level of abstraction per function.** A function either orchestrates (calls other functions) or implements (does the work), never both.
3. **Names reveal intent.** `consumeIngredients()` not `processInputs()`. `xpForLevel()` not `calcLvl()`.
4. **Functions do one thing.** `computeAllStats()` calls six category functions. Each category function does one category. No function exceeds 30 lines.
5. **No premature abstraction.** Do not extract an interface until there are two concrete implementations. Do not build a plugin system for one plugin.
6. **Fail fast, fail visibly.** Validate all registry lookups at startup. Throw descriptive errors with context (`Unknown species 42`), not generic `nullptr` or -1.
7. **RAII everywhere.** Resources are owned by objects that release them in destructors. No manual `delete`, no raw `new` outside factories.

### 1.2 Performance Tenets

1. **Zero allocations on hot paths.** No `std::vector::push_back` in update loops, physics, or per-frame logic. Pre-allocate everything.
2. **SoA storage over AoS.** `ComponentStore<T>` with dense/sparse arrays is mandatory for any component that more than one system touches per frame.
3. **Branchless where possible.** Use bitwise tricks, precomputed lookup tables, and arithmetic over conditionals in inner loops.
4. **Data-oriented iteration.** Systems iterate component rows, not entity lists. The `queryEntities()` function finds the smallest component store and iterates that, yielding contiguous cache lines.
5. **Worker thread isolation.** Expensive computations (worldgen, meshing) run on `WorkerThreadPool`. The main thread never blocks on them.
6. **Measure, don't guess.** Every optimization must be justified by a profile. Never optimize a path that accounts for <5% of frame time.

### 1.3 Technical Debt Prevention

1. **Every public API has exactly one consumer.** If two systems need the same data, they share the component, not a utility class.
2. **No cyclic dependencies.** `src/content/` never imports from `src/engine/systems/`. Systems depend on components, components depend on nothing.
3. **Dead code is deleted, not commented out.** If a feature is postponed, remove the code. Git history preserves it.
4. **Tests accompany every registry and formula.** `xpForLevel()` has a test for level 1, 2, 10, 50, 100. `computeCombatStats()` has a test with known input/output pairs.
5. **Header hygiene.** `.hpp` files should expose only public interfaces. Implementation details go in `.cpp` files or anonymous namespaces.

---

## 2. Design Patterns

### 2.1 Composition Root (Dependency Injection Without a Framework)

All object wiring happens in exactly one place: the `Game` constructor in `src/game/Game.hpp`. There is no dependency injection container, no service locator, no global singleton.

```cpp
// src/game/Game.cpp — Composition Root (conceptual C++ extension)

Game::Game(GLFWwindow* window, const GameConfig& config, Options options)
  : m_window(window), m_config(config),
    m_session(config.renderDistance),
    m_blocks(4096),
    m_worldGenPipeline(config.worldSeed),
    m_transforms(m_entityManager.capacity()),
    m_bodies(m_entityManager.capacity()),
    m_players(m_entityManager.capacity())
{
  // 1. Infrastructure — no business logic
  VanillaBlockFactory{}.registerAll(m_blocks);

  int32_t poolCap = ...;
  m_pool = SharedPool::create(poolCap, makeDims(config));
  m_world.reset(new World(...));
  m_renderer = std::make_unique<Renderer>(window, m_blocks, config);

  // 2. Content registries — data definitions
  ItemRegistry itemRegistry;
  FloraRegistry floraRegistry;
  ClassRegistry classRegistry;
  SpeciesRegistry speciesRegistry;

  // 3. ECS — entities are just IDs
  EntityManager em{1 << 12};
  ComponentStore<cmp::Transform> transforms(em.capacity());
  ComponentStore<cmp::RigidBody> bodies(em.capacity());
  ComponentStore<cmp::Health> health(em.capacity());

  // 4. Factories — assemble entities from data
  EnhancedMobFactory mobFactory(em, transforms, bodies, health, /*...*/);

  // 5. Systems — operate on components
  SystemManager<Game> systems;
  systems.add(std::make_unique<PhysicsSystem>(em, transforms, bodies, *m_world));
  systems.add(std::make_unique<CropGrowthSystem>(*m_world, floraRegistry));
  // Systems added in dependency order — SystemManager sorts by stage
}
```

**Rules:**
- `new` only appears in the composition root (or inside factory classes that are themselves constructed in the root).
- No system knows about any other system. Systems depend on components, not on other systems.
- If two systems need to communicate, they do so through the `EventBus` or through shared component state.

### 2.2 Abstract Factory Pattern

Used wherever a family of related objects needs to be created without specifying concrete classes.

```
┌─────────────────────┐     ┌──────────────────────────┐
│ CharacterFactory    │────▶│ CharacterFactoryImpl     │
│ (interface)         │     │  - composes species/     │
│  createCharacter()  │     │    class/background      │
│  rollRandomParams() │     │  - writes components     │
└─────────────────────┘     └──────────────────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
          ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
          │ SpeciesReg.  │  │ ClassReg.   │  │ BkgdReg.    │
          │  (data)      │  │  (data)     │  │  (data)     │
          └───────────────┘  └─────────────┘  └─────────────┘
```

Each registry is a plain data map loaded at startup. Factories are stateless — they compose data from registries and write into components.

**Why this avoids technical debt:**
- Adding a new species, class, or background requires zero new code — only new data entries.
- The factory interface allows swapping implementations (e.g., a `DebugCharacterFactory` for testing).
- No switch statements on type — the `ClassRegistry::get()` lookup is O(1).

### 2.3 Strategy Pattern (Stateless Computations)

Stat formulas use the Strategy pattern internally: each derived stat category is a pure function that can be tested, replaced, or composed independently.

```cpp
// /src/content/stats/StatFormulas.hpp

// Each category is a pure function — same inputs always produce same outputs.
// No state. No side effects. No allocations.

export type StatCategoryFunction<T> = (attrs: AttributeSet, level: number) => T;

// Registry of stat category functions, allowing runtime extension
const STAT_CATEGORIES = new Map<string, StatCategoryFunction<any>>();

export function registerStatCategory(name: string, fn: StatCategoryFunction<any>): void {
  STAT_CATEGORIES.set(name, fn);
}

// Built-in categories
registerStatCategory("combat", computeCombatStats);
registerStatCategory("defense", computeDefenseStats);
registerStatCategory("magic", computeMagicStats);

// A mod or DLC can add new categories:
// registerStatCategory("eldritch", computeEldritchStats);
```

**Why this avoids technical debt:**
- New stat categories can be added without modifying existing code (Open/Closed principle).
- Each formula is independently unit-testable.
- The category registry is extensible by content packs without touching engine code.

### 2.4 Observer Pattern (EventBus)

Systems communicate significant events through a typed event bus. This prevents direct coupling between systems.

```cpp
// src/engine/core/EventBus.hpp — Type-erased event bus (already exists)

// Events are plain structs forwarded to listeners:
struct LevelUpEvent {
  int32_t entityIndex;
  int32_t newLevel;
  // ...
};

// Usage in systems:
class LevelingSystem {
public:
  LevelingSystem(ComponentStore<cmp::Character>& characters, EventBus& eventBus)
    : m_characters(characters), m_eventBus(eventBus) {}

  void addExperience(int32_t entityIndex, float amount) {
    auto* row = m_characters.tryGet(entityIndex);
    if (!row) return;
    row->experience += amount;
    // Emit event so other systems react
    m_eventBus.emit(LevelUpEvent{entityIndex, row->level});
  }

private:
  ComponentStore<cmp::Character>& m_characters;
  EventBus& m_eventBus;
};
```

**Why this avoids technical debt:**
- `QuestTrackingSystem` has zero knowledge of `LevelingSystem` or `CombatSystem`.
- Events can be added without modifying existing subscribers.
- Testing: mock the event bus and verify emissions instead of wiring real systems.

### 2.5 Data-Oriented ECS (Existing Pattern, Extended)

The existing `ComponentStore` with SoA TypedArrays is the cornerstone of performance. The RPG systems extend this pattern:

```cpp
// CORRECT — SoA via ComponentStore<T>, iteration is cache-friendly
void updateManaRegen(ComponentStore<cmp::Character>& characters, float dt) {
  characters.forEach([](int32_t, int32_t, cmp::Character& c) {
    c.mana = std::min(c.maxMana, c.mana + c.manaRegen * dt);
  });
}
```

### 2.6 Command Pattern (Inventory & Process Actions)

Player actions (craft, smelt, repair, move item) are modeled as command objects. This enables undo, replay, and network synchronization.

Commands are function objects in C++ — see the original design doc for concepts.

**Why this avoids technical debt:**
- Commands can be serialized for replay debugging.
- Multiplayer: commands are sent over the network, executed deterministically.
- Undo: crafting errors can refund materials.

---

## 3. Dependency Graph & Layering

### 3.1 Strict Layering

```
src/
├── engine/              ← NO imports from game/ or content/
│   ├── core/            ← NO imports from ecs/, render/, workers/
│   ├── alloc/           ← NO imports outside engine/
│   ├── math/            ← NO imports outside engine/
│   ├── ecs/components/  ← Import from ComponentStore only (dependencies flow up)
│   ├── ecs/systems/     ← Import components, never other systems
│   ├── render/          ← Import ecs/components for data only
│   └── workers/         ← Import world/ blocks for data definitions
│
├── world/               ← NO imports from game/ or content/
│   ├── blocks/          ← Data definitions, no logic
│   └── Chunk.hpp, World.hpp, MaterialRegistry.hpp
│
├── content/             ← Imports from engine/ and world/ only
│   ├── items/           ← Item definitions, factories
│   ├── classes/         ← Class definitions
│   ├── species/         ← Species definitions
│   ├── backgrounds/     ← Background definitions
│   ├── stats/           ← Pure stat formulas
│   ├── processes/       ← Transformation recipes
│   ├── magic/           ← Spell definitions
│   ├── quests/          ← Quest definitions
│   ├── factions/        ← Faction definitions
│   ├── flora/           ← Flora definitions
│   └── loot/            ← Loot table definitions
│
├── game/                ← Imports everything (composition root)
│   ├── Game.cpp         ← Only file that wires all systems together (composition root)
│   ├── inventory/       ← Inventory interaction logic
│   └── commands/        ← Command pattern implementations
│
├── ui/                  ← Imports from engine/ and content/ for read-only access
│   ├── character/       ← Character creation screens
│   ├── station/         ← Crafting station screens
│   └── hud/             ← HUD elements
│
└── main.cpp             ← Bootstrap, creates Game
```

### 3.2 What Breaks If You Import Wrong

```
🚫 content/ importing from game/ 
   → Content can't be reused across projects (e.g., headless server)
   → Creates circular dependency risk at the composition root

🚫 engine/systems/ importing from content/
   → Engine becomes coupled to specific content
   → Adding a new species requires modifying engine code

🚫 engine/ecs/components/ importing from engine/ecs/systems/
   → Components are pure data — they must not depend on logic

✅ Correct: systems depend on components (data), not on other systems
✅ Correct: content depends on engine types (interfaces, enums, ComponentStore)
✅ Correct: game (composition root) depends on everything — that's its job
```

### 3.3 Enforcing the Layering

```typescript
// eslint.config.js (conceptual)
export default [
  {
    rules: {
      'no-restricted-imports': [
        'error',
        {
          patterns: [
            // engine/ must not import from game/ or content/
            {
              group: ['src/game/**', 'src/content/**'],
              from: 'src/engine/**',
              message: 'Engine must not depend on game or content modules.',
            },
            // content/ must not import from game/
            {
              group: ['src/game/**'],
              from: 'src/content/**',
              message: 'Content must not depend on game modules.',
            },
            // components must not import from systems
            {
              group: ['src/engine/ecs/systems/**'],
              from: 'src/engine/ecs/components/**',
              message: 'Components must not depend on systems.',
            },
          ],
        },
      ],
    },
  },
];
```

---

## 4. Avoiding Technical Debt — Concrete Rules

### 4.1 Data Definitions vs Logic

```
┌─────────────────────────────────────────────────────────────┐
│                     Data Definitions                         │
│  (plain objects, interfaces, const arrays)                  │
│  Live in: src/content/*, src/world/blocks/*                 │
│  Changes: adding new data requires zero code changes        │
│  Loaded: at startup, frozen, never mutated at runtime       │
├─────────────────────────────────────────────────────────────┤
│                     Logic / Systems                          │
│  (pure functions, ECS systems, factories)                   │
│  Live in: src/engine/ecs/systems/*, src/content/stats/*     │
│  Changes: algorithmic improvements, new mechanics           │
│  Run: every frame, on worker threads, on demand             │
└─────────────────────────────────────────────────────────────┘
```

**Rule:** If you are adding a new *item*, you should write zero logic code — only data in an array. If you are adding a new *mechanic* (e.g., "items can be dyed"), you write logic once and add data for each dyeable item.

### 4.2 Data-Driven Assets

Visual assets (textures, materials, models) must be completely decoupled from C++ code. The engine should never use hardcoded enumerations (e.g., `enum class Tex`) or hardcoded color generation (e.g., `makeLayer()`) for textures.

Instead, a centralized **Asset Manager** should load these at startup from external JSON/YAML definitions and PNG files. Adding a new block texture should require dropping a PNG into an `assets/` folder and updating a JSON file, requiring zero C++ recompilation.

### 4.3 The Switch Statement Rule

Switch statements on type enums are a debt magnet. Every new enum value requires updating every switch.

```typescript
// 🚫 BAD — adding a new class requires updating this switch
function getStartingEquipment(classId: ClassId): number[] {
  switch (classId) {
    case ClassId.WARRIOR: return [256, 272, 1, 5];
    case ClassId.MAGE: return [300, 1];
    // ... forgot to add new class here → runtime bug
  }
}

// ✅ GOOD — data-driven, no switch needed
function getStartingEquipment(classId: ClassId): number[] {
  return CLASS_DEFINITIONS[classId].startingEquipment;
}

// 🚫 BAD — switch on species for innate abilities
function getInnateAbility(speciesId: SpeciesId): number {
  switch (speciesId) {
    case SpeciesId.ELF: return 100; // Night Vision
    // ...
  }
}

// ✅ GOOD — data-driven
function getInnateAbility(speciesId: SpeciesId): readonly number[] {
  return SPECIES_DEFINITIONS[speciesId].innateAbilities;
}
```

**Exception:** Switches on *primitive input* (keyboard keys, mouse buttons, network opcodes) are acceptable because those are external protocols, not internal types.

### 4.3 The Feature Flag Pattern

For incomplete features, use feature flags rather than commented-out code or unfinished implementations:

```typescript
// /src/engine/core/Config.hpp

export const FEATURE_FLAGS = {
  /** Enable the spell casting system (WIP). */
  magicSystem: false,
  /** Enable NPC daily schedules (coming in v0.5). */
  npcSchedules: false,
  /** Enable the durability system for tools. */
  toolDurability: true,
  /** Enable crop growth over time. */
  cropGrowth: true,
} as const;

// Usage:
export class CropGrowthSystem {
  update(dt: number): void {
    if (!FEATURE_FLAGS.cropGrowth) return; // ← single gate
    // ... growth logic
  }
}
```

When the feature is complete, remove the flag and the dead branch. Never leave a feature flag permanently true.

### 4.4 Defensive Registry Access

```typescript
// /src/content/classes/ClassRegistry.hpp

export class ClassRegistry {
  private readonly classes = new Map<number, ClassDefinition>();

  get(classId: number): ClassDefinition {
    const def = this.classes.get(classId);
    if (!def) {
      // Fail fast with a descriptive message during development
      throw new Error(
        `ClassRegistry: unknown class ID ${classId}. ` +
        `Registered IDs: [${Array.from(this.classes.keys()).join(', ')}]. ` +
        `Did you forget to register a class in CLASS_DEFINITIONS?`
      );
    }
    return def;
  }

  /** Safe variant for systems that may query non-existent IDs from dynamic data. */
  tryGet(classId: number): ClassDefinition | undefined {
    return this.classes.get(classId);
  }
}
```

### 4.5 The Component Store Pattern — Write-Once, Read-Many

Components are written by the factory at entity creation time, then read by multiple systems each frame. Mutable state (HP, mana, position) is the exception, not the rule.

```typescript
// ✅ GOOD — immutable data (written once at creation)
export const CharacterDesc = {
  baseStrength: { type: Uint8Array, length: 1 },  // never changes after creation
  baseDexterity: { type: Uint8Array, length: 1 },
  // ... 
  classId: { type: Uint8Array, length: 1 },
  speciesId: { type: Uint8Array, length: 1 },
};

// ✅ GOOD — mutable state (changes during gameplay)
export const CharacterDesc = {
  mana: { type: Float32Array, length: 1 },     // changes every frame
  stamina: { type: Float32Array, length: 1 },  // changes every frame
  experience: { type: Uint32Array, length: 1 }, // changes occasionally
};
```

**Performance note:** Separating hot (per-frame) data from cold (once-per-creation) data into different component stores means systems that only need hot data iterate smaller arrays. Consider splitting:

```typescript
// Hot path — iterated by ManaRegenSystem every frame
export const ManaDesc = {
  mana: { type: Float32Array, length: 1 },
  maxMana: { type: Float32Array, length: 1 },
  manaRegen: { type: Float32Array, length: 1 },
};

// Cold path — read once when opening character sheet
export const CharacterIdentityDesc = {
  characterName: { type: Uint32Array, length: 8 },
  level: { type: Uint16Array, length: 1 },
  classId: { type: Uint8Array, length: 1 },
};
```

---

## 5. Performance Architecture

### 5.1 Hot Path Identification

```
Hot Path (every frame, must complete in <1ms):
├── PhysicsSystem       — swept AABB vs terrain grid
├── PlayerController    — input → transform
├── ManaRegenSystem     — mana += regen * dt
├── NpcScheduleSystem   — pathfinding for active NPCs
├── QuestTrackingSystem — objective checks
└── Renderer            — frustum cull + draw calls

Warm Path (every few frames, <5ms):
├── CropGrowthSystem    — random tick on loaded chunks
├── ReputationSystem    — decay calculations
├── DurabilitySystem    — tool wear checks

Cold Path (on event, <50ms):
├── CharacterFactory    — entity creation
├── ProcessExecution    — crafting, smelting, alchemy
├── LevelingSystem      — stat recomputation on level up
└── SaveManager         — serialization
```

### 5.2 Optimization Rules by Path

| Path | Allocation Budget | Branching | Pattern | Memory / CPU specific |
|:-----|:-----------------|:----------|:--------|:----------------------|
| Hot | Zero allocations | Minimize branches | SoA iteration, precomputed LUTs | `std::pmr` bump allocators, AVX2 SIMD intrinsics |
| Warm | Zero allocations | Some branches OK | SoA iteration | `std::pmr` object pools |
| Cold | Allocation OK | Any branches OK | Factory pattern, composition | Standard heap allocation OK |

### 5.3 Precomputed Lookup Tables

For any formula used more than 100 times per second, precompute a lookup table at startup:

```typescript
// /src/content/stats/XpLookup.hpp

/**
 * XP table is computed once at startup and reused for the entire session.
 * This avoids calling Math.pow() thousands of times during level-up checks.
 */
export class XpLookup {
  private readonly xpForLevel: Uint32Array;
  private readonly totalXpForLevel: Uint32Array;

  constructor(maxLevel: number = 999) {
    this.xpForLevel = new Uint32Array(maxLevel + 1);
    this.totalXpForLevel = new Uint32Array(maxLevel + 1);
    
    let total = 0;
    for (let level = 1; level <= maxLevel; level++) {
      const xp = Math.floor(Math.pow(level, 1.8) + level * 50);
      this.xpForLevel[level] = xp;
      total += xp;
      this.totalXpForLevel[level] = total;
    }
  }

  xpNeededFor(level: number): number {
    return this.xpForLevel[level] ?? 0xFFFFFFFF;
  }

  totalXpFor(level: number): number {
    return this.totalXpForLevel[level] ?? 0xFFFFFFFF;
  }

  levelAtTotalXp(totalXp: number): number {
    // Binary search on totalXpForLevel
    let lo = 1, hi = this.totalXpForLevel.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >>> 1;
      if (this.totalXpForLevel[mid] <= totalXp) lo = mid;
      else hi = mid - 1;
    }
    return lo;
  }
}

// Singleton — computed once
export const XP_LOOKUP = new XpLookup(999);
```

### 5.4 Profiling Hooks

Every system emits timing data that can be toggled at runtime:

```typescript
// /src/engine/core/Profiler.hpp

export class Profiler {
  private readonly timings = new Map<string, number[]>();
  private readonly maxSamples = 100;
  private enabled = false;

  begin(systemName: string): number {
    if (!this.enabled) return 0;
    return performance.now();
  }

  end(systemName: string, startTime: number): void {
    if (!this.enabled) return;
    const elapsed = performance.now() - startTime;
    let samples = this.timings.get(systemName);
    if (!samples) {
      samples = [];
      this.timings.set(systemName, samples);
    }
    samples.push(elapsed);
    if (samples.length > this.maxSamples) samples.shift();
  }

  enable(): void { this.enabled = true; }
  disable(): void { this.enabled = false; }

  /** Returns average, min, max for each system. */
  report(): Record<string, { avg: number; min: number; max: number }> {
    const report: Record<string, { avg: number; min: number; max: number }> = {};
    for (const [name, samples] of this.timings) {
      const sum = samples.reduce((a, b) => a + b, 0);
      report[name] = {
        avg: sum / samples.length,
        min: Math.min(...samples),
        max: Math.max(...samples),
      };
    }
    return report;
  }
}

// Usage in any system:
export class PhysicsSystem {
  update(state: Game, dt: number): void {
    const start = state.profiler.begin('physics');
    // ... physics logic ...
    state.profiler.end('physics', start);
  }
}
```

---

## 6. Clean Code Examples

### 6.1 Good: Stateless Pure Function

```typescript
// /src/content/stats/DiminishedValue.hpp
// Pure function: same inputs, same outputs. No side effects. No state.
// Testable in isolation. Cacheable. Parallelizable.

export function diminish(rawValue: number, softCap: number = 30): number {
  if (rawValue <= softCap) return rawValue;
  return softCap + (rawValue - softCap) * (softCap / (rawValue - softCap + softCap));
}
```

### 6.2 Good: Single Responsibility System

```typescript
// /src/engine/ecs/systems/ManaRegenSystem.hpp
// Does exactly one thing: tick mana regen for all characters.
// Does not touch HP, stamina, or anything else.

export class ManaRegenSystem implements System<Game> {
  readonly name = 'manaRegen';
  readonly stage = 'postPhysics';

  constructor(
    private readonly characters: ComponentStore<typeof CharacterDesc>,
  ) {}

  update(_state: Game, dt: number): void {
    const { mana, maxMana, manaRegen } = this.characters.data;

    for (const row of this.characters.rows()) {
      mana[row] = Math.min(maxMana[row], mana[row] + manaRegen[row] * dt);
    }
  }
}
```

### 6.3 Bad: God System (Never Write This)

```typescript
// 🚫 BAD — this system does everything
export class PlayerUpdateSystem implements System<Game> {
  readonly name = 'playerUpdate';
  readonly stage = 'postPhysics';

  update(state: Game, dt: number): void {
    // Move player
    // Check for level up
    // Regen mana
    // Regen HP
    // Process crafting queue
    // Update quest progress
    // Check faction reputation
    // Auto-save
    // ... this method will be 500 lines by v0.5
  }
}
```

**Refactored into:**

```
PlayerControllerSystem  → moves the player
LevelingSystem          → XP and level-ups
ManaRegenSystem         → mana regen
HealthSystem            → HP regen and damage
ProcessExecutionSystem  → crafting, smelting
QuestTrackingSystem     → quest objectives
ReputationSystem        → faction reputation
SaveManager             → persistence (separate from update loop entirely)
```

### 6.4 Good: Defensive Copy for Public APIs

```typescript
// /src/content/classes/ClassRegistry.hpp

export class ClassRegistry {
  private readonly classes = new Map<number, ClassDefinition>();

  getAll(): readonly ClassDefinition[] {
    // Return a frozen copy so callers can't mutate the registry
    return Object.freeze([...this.classes.values()]);
  }

  get(classId: number): ClassDefinition {
    const def = this.classes.get(classId);
    if (!def) throw new Error(`Unknown class ${classId}`);
    return def; // OK to return the reference — data is const at runtime
  }
}
```

### 6.5 Good: Small, Focused Interfaces

```typescript
// /src/content/flora/FloraTypes.hpp

// Each interface captures exactly one concern
export interface LightRequirements {
  readonly minSkyLight: number;
  readonly maxSkyLight: number;
  readonly minBlockLight: number;
}

export interface SoilPreference {
  readonly acceptableSoil: readonly SoilType[];
  readonly requiresHydration: boolean;
  readonly hydrationRadius: number;
}

export interface GrowthConfig {
  readonly stages: readonly number[];        // block IDs per stage
  readonly growthTicks: number;
  readonly lightRequirements: LightRequirements;
  readonly boneMealable: boolean;
  readonly deathConditions?: DeathCondition[];
}

// Composed into FloraProperties
export interface FloraProperties {
  readonly materialId: number;
  readonly name: string;
  readonly renderType: FloraRenderType;
  readonly soil: SoilPreference;
  readonly growth: GrowthConfig;
  readonly drops: DropConfig;
}
```

---

## 7. Implementation Roadmap — Debt-Free Ordering

### Phase 1: Foundation (No game logic yet)
1. `StatFormulas.hpp` — pure functions, zero dependencies, fully tested
2. `ClassRegistry`, `SpeciesRegistry`, `BackgroundRegistry` — data definitions
3. `CharacterDesc` component — SoA TypedArray, extends existing ECS
4. `CharacterFactoryImpl` — assembles entities from registries
5. `EventBus<GameEvents>` — typed event definitions

### Phase 2: Core Systems (Single-responsibility systems)
6. `ManaRegenSystem` — tiny, focused, tested
7. `LevelingSystem` — XP accumulation, level-up event emission
8. `ModifierSystem` — temporary buff/debuff application and expiry
9. `DurabilitySystem` — tool/armor wear tracking

### Phase 3: Interaction Systems (Event-driven)
10. `ProcessRegistry` + `ProcessExecutionSystem` — crafting/smelting/alchemy
11. `QuestTrackingSystem` — listens to events, updates objective progress
12. `ReputationSystem` — faction reputation changes
13. `TradingSystem` — buy/sell with price calculation

### Phase 4: World Integration (Worker-safe)
14. `FloraDecorator` — worldgen flora placement (in WorldGenWorker)
15. `CropGrowthSystem` — random tick growth on loaded chunks
16. `SpellCastingSystem` — cast timer, mana cost, spell effects

### Phase 5: NPC & AI (ECS extension)
17. `NpcSpawnSystem` — procedural NPC generation
18. `NpcScheduleSystem` — daily routine execution
19. `DialogueSystem` — conversation tree traversal

### Phase 6: UI (read-only data access)
20. `CharacterCreationUI` — multi-step creation flow
21. `CharacterSheetUI` — stat display, equipment, skills
22. `StationUI` — crafting station interfaces

---

## 8. Testability

### 8.1 What to Test

| Layer | Test Strategy | Example |
|:------|:--------------|:--------|
| **Stat formulas** | Unit test with known inputs/outputs | `diminish(30) === 30`, `diminish(50) === 42.5` |
| **Registries** | Test every entry has required fields | `CLASS_DEFINITIONS.forEach(c => expect(c.baseStats).toBeDefined())` |
| **Factories** | Create character, verify component values | `factory.createCharacter(params); expect(charData.level[0]).toBe(5)` |
| **Systems** | Add component, run update, assert change | `mana[0] = 0; system.update(); expect(mana[0]).toBeGreaterThan(0)` |
| **Events** | Subscribe, emit, verify callback | `const spy = jest.fn(); bus.on('mob.killed', spy); bus.emit(...); expect(spy).toHaveBeenCalled()` |
| **No integration tests** | — | Systems are decoupled by design; integration is the composition root |

### 8.2 Testing Patterns

```cpp
// tests/test_stat_system.cpp — Catch2 test
TEST_CASE("xpForLevel produces correct values", "[stats]") {
  REQUIRE(xpForLevel(1) == 0);
  REQUIRE(xpForLevel(2) == 100);
  REQUIRE(xpForLevel(10) == 4500);
}
```

---

## 9. Summary of Patterns & Where They Apply

| Pattern | Where Used | Why |
|:--------|:-----------|:----|
| **Composition Root** | `Game.cpp` | Single place where all objects are wired. No DI container needed. |
| **Abstract Factory** | `CharacterFactory`, `MobFactory`, `ItemFactory` | Encapsulates complex object creation. Swap implementations for testing. |
| **Strategy** | `StatFormulas.hpp` category functions | Each formula is independently testable and replaceable. |
| **Observer (EventBus)** | Cross-system communication | `QuestTrackingSystem` listens to `mob.killed` without knowing about combat. |
| **Command** | `CraftCommand`, `MoveItemCommand` | Enables undo, replay, network replication. |
| **Data-Oriented ECS** | All `ComponentStore` usage | Cache-friendly iteration. Zero GC pressure. |
| **Lookup Table** | `XpLookup`, damage tables | Precompute expensive math at startup, O(1) at runtime. |
| **Feature Flag** | `FEATURE_FLAGS.magicSystem` | Ship incomplete features behind a toggle. Remove toggle when done. |
| **Disposable** | GPU resources, workers, audio | RAII-style cleanup. Prevent resource leaks. |
| **Registry** | All content registration | Data-driven design. Adding content requires no code changes. |

---

## 10. Enforcement Tools

```cmake
# CMakeLists.txt — Compiler warnings enforce architecture
target_compile_options(terrain_app PRIVATE
  -Wall -Wextra -Wpedantic
  -Werror=shadow -Werror=return-type
  -Wno-unused-parameter
)

# Layer enforcement is manual (code review), but the directory layout
# mirrors the TypeScript original — engine/ never includes from game/ or content/
```

```cmake
# CMakeLists.txt — Strict compiler flags prevent common debt patterns
target_compile_options(terrain_app PRIVATE
  -Wall -Wextra -Wpedantic
  -Werror=return-type -Werror=unused-result
  $<$<CONFIG:Release>:-O2 -DNDEBUG>
  $<$<CONFIG:Debug>:-g -O0 -DDEBUG>
)
target_compile_features(terrain_app PRIVATE cxx_std_20)
    "noFallthroughCasesInSwitch": true,
    "exactOptionalPropertyTypes": true,
    "forceConsistentCasingInFileNames": true
  }
}
```
