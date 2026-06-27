To complete the game's content layer, we will implement the **Block Abstract Factory** and the **Mob ECS Factory**.

Both systems are designed to populate the DOD-based `ComponentStore`s and the `MaterialRegistry` without allocating per-instance objects during the game loop. Mobs are not classes; they are simply Entity IDs with a specific configuration of components injected into tightly packed `TypedArray`s.


---

### 1. Block Types (Abstract Factory & Texture Array Mapping)

We define a strict mapping for the `TEXTURE_2D_ARRAY` layers, then use the `VanillaBlockFactory` to populate the `MaterialRegistry` with 1.5.2-era blocks (IDs closely matching classic Minecraft).

```typescript
// /src/world/blocks/TextureLayers.ts
/**
 * Strict mapping of texture names to their index in the WebGL2 TEXTURE_2D_ARRAY.
 * Using a const enum allows TypeScript to inline these at compile time,
 * eliminating object property lookup overhead in the mesher hot path.
 */
export const enum Tex {
  AIR = 0,
  STONE = 1, GRASS_TOP = 2, GRASS_SIDE = 3, DIRT = 4,
  COBBLESTONE = 5, PLANKS_OAK = 6, LOG_OAK_TOP = 7, LOG_OAK_SIDE = 8,
  LEAVES_OAK = 9, GLASS = 10, SAND = 11, GRAVEL = 12,
  COAL_ORE = 13, IRON_ORE = 14, GOLD_ORE = 15, DIAMOND_ORE = 16,
  REDSTONE_ORE = 17, LAPIS_ORE = 18, BEDROCK = 19, WATER = 20,
  LAVA = 21, BRICK = 22, TNT_SIDE = 23, TNT_TOP = 24, TNT_BOTTOM = 25,
  MOSSY_COBBLESTONE = 26, OBSIDIAN = 27, CRAFTING_TABLE_TOP = 28,
  CRAFTING_TABLE_SIDE = 29, CRAFTING_TABLE_FRONT = 30, FURNACE_FRONT = 31,
  FURNACE_SIDE = 32, FURNACE_TOP = 33, SPONGE = 34, DIAMOND_BLOCK = 35,
  GOLD_BLOCK = 36, IRON_BLOCK = 37, SNOW = 38, ICE = 39,
  CACTUS_TOP = 40, CACTUS_SIDE = 41, CACTUS_BOTTOM = 42,
  // ... 1.5.2 era textures ...
}

// /src/world/blocks/VanillaBlockFactory.ts
import { MaterialFactory, MaterialRegistry, MaterialDefinition, AABB } from "./MaterialDefinition";
import { Tex } from "./TextureLayers";

const FULL_CUBE: AABB = { minX: 0, minY: 0, minZ: 0, maxX: 1, maxY: 1, maxZ: 1 };
const EMPTY_AABB: AABB = { minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0 };

export class VanillaBlockFactory implements MaterialFactory {
  registerAll(registry: MaterialRegistry): void {
    // --- Helpers to keep registration strictly typed and DRY ---
    const opaque = (id: number, name: string, tex: { top: number; bottom: number; side: number }, collision: AABB = FULL_CUBE): void => {
      registry.register({ id, name, textures: tex, material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 }, collision });
    };
    const transparent = (id: number, name: string, tex: { top: number; bottom: number; side: number }, isFoliage: boolean = false, collision: AABB = FULL_CUBE): void => {
      registry.register({ id, name, textures: tex, material: { opaque: false, transparent: true, liquid: false, foliage: isFoliage, lightEmission: 0 }, collision });
    };
    const liquid = (id: number, name: string, tex: number, light: number = 0): void => {
      registry.register({ id, name, textures: { top: tex, bottom: tex, side: tex }, material: { opaque: false, transparent: true, liquid: true, foliage: false, lightEmission: light }, collision: EMPTY_AABB });
    };
    const emitter = (id: number, name: string, tex: { top: number; bottom: number; side: number }, level: number): void => {
      registry.register({ id, name, textures: tex, material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: level }, collision: FULL_CUBE });
    };

    // --- Classic 1.5.2 Era Block Definitions ---
    opaque(1, "stone", { top: Tex.STONE, bottom: Tex.STONE, side: Tex.STONE });
    opaque(2, "grass", { top: Tex.GRASS_TOP, bottom: Tex.DIRT, side: Tex.GRASS_SIDE });
    opaque(3, "dirt", { top: Tex.DIRT, bottom: Tex.DIRT, side: Tex.DIRT });
    opaque(4, "cobblestone", { top: Tex.COBBLESTONE, bottom: Tex.COBBLESTONE, side: Tex.COBBLESTONE });
    opaque(5, "wood_planks", { top: Tex.PLANKS_OAK, bottom: Tex.PLANKS_OAK, side: Tex.PLANKS_OAK });
    opaque(7, "bedrock", { top: Tex.BEDROCK, bottom: Tex.BEDROCK, side: Tex.BEDROCK });
    transparent(8, "water", { top: Tex.WATER, bottom: Tex.WATER, side: Tex.WATER }, false, EMPTY_AABB); // Override to liquid via direct register below to save space, but left here for pattern
    
    // Refined specific blocks
    registry.register({ id: 8, name: "water", textures: { top: Tex.WATER, bottom: Tex.WATER, side: Tex.WATER }, material: { opaque: false, transparent: true, liquid: true, foliage: false, lightEmission: 0 }, collision: EMPTY_AABB });
    registry.register({ id: 9, name: "still_water", textures: { top: Tex.WATER, bottom: Tex.WATER, side: Tex.WATER }, material: { opaque: false, transparent: true, liquid: true, foliage: false, lightEmission: 0 }, collision: EMPTY_AABB });
    registry.register({ id: 10, name: "lava", textures: { top: Tex.LAVA, bottom: Tex.LAVA, side: Tex.LAVA }, material: { opaque: false, transparent: true, liquid: true, foliage: false, lightEmission: 15 }, collision: EMPTY_AABB });
    
    opaque(12, "sand", { top: Tex.SAND, bottom: Tex.SAND, side: Tex.SAND });
    opaque(13, "gravel", { top: Tex.GRAVEL, bottom: Tex.GRAVEL, side: Tex.GRAVEL });
    opaque(14, "gold_ore", { top: Tex.GOLD_ORE, bottom: Tex.GOLD_ORE, side: Tex.GOLD_ORE });
    opaque(15, "iron_ore", { top: Tex.IRON_ORE, bottom: Tex.IRON_ORE, side: Tex.IRON_ORE });
    opaque(16, "coal_ore", { top: Tex.COAL_ORE, bottom: Tex.COAL_ORE, side: Tex.COAL_ORE });
    opaque(17, "log", { top: Tex.LOG_OAK_TOP, bottom: Tex.LOG_OAK_TOP, side: Tex.LOG_OAK_SIDE });
    transparent(18, "leaves", { top: Tex.LEAVES_OAK, bottom: Tex.LEAVES_OAK, side: Tex.LEAVES_OAK }, true); // Foliage flag enables alpha-test
    transparent(20, "glass", { top: Tex.GLASS, bottom: Tex.GLASS, side: Tex.GLASS }, false);
    opaque(21, "lapis_ore", { top: Tex.LAPIS_ORE, bottom: Tex.LAPIS_ORE, side: Tex.LAPIS_ORE });
    opaque(22, "lapis_block", { top: Tex.LAPIS_ORE, bottom: Tex.LAPIS_ORE, side: Tex.LAPIS_ORE }); // placeholder tex
    opaque(23, "sandstone", { top: Tex.SAND, bottom: Tex.SAND, side: Tex.SAND });
    
    // Partial AABB example: Cactus (1.5.2)
    registry.register({ 
      id: 24, name: "cactus", 
      textures: { top: Tex.CACTUS_TOP, bottom: Tex.CACTUS_BOTTOM, side: Tex.CACTUS_SIDE }, 
      material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
      collision: { minX: 0.0625, minY: 0, minZ: 0.0625, maxX: 0.9375, maxY: 1, maxZ: 0.9375 } 
    });

    opaque(41, "gold_block", { top: Tex.GOLD_BLOCK, bottom: Tex.GOLD_BLOCK, side: Tex.GOLD_BLOCK });
    opaque(42, "iron_block", { top: Tex.IRON_BLOCK, bottom: Tex.IRON_BLOCK, side: Tex.IRON_BLOCK });
    opaque(45, "brick", { top: Tex.BRICK, bottom: Tex.BRICK, side: Tex.BRICK });
    opaque(48, "mossy_cobblestone", { top: Tex.MOSSY_COBBLESTONE, bottom: Tex.MOSSY_COBBLESTONE, side: Tex.MOSSY_COBBLESTONE });
    opaque(49, "obsidian", { top: Tex.OBSIDIAN, bottom: Tex.OBSIDIAN, side: Tex.OBSIDIAN });
    opaque(54, "chest", { top: Tex.PLANKS_OAK, bottom: Tex.PLANKS_OAK, side: Tex.PLANKS_OAK }); // Simplified
    opaque(56, "diamond_ore", { top: Tex.DIAMOND_ORE, bottom: Tex.DIAMOND_ORE, side: Tex.DIAMOND_ORE });
    opaque(57, "diamond_block", { top: Tex.DIAMOND_BLOCK, bottom: Tex.DIAMOND_BLOCK, side: Tex.DIAMOND_BLOCK });
    opaque(58, "crafting_table", { top: Tex.CRAFTING_TABLE_TOP, bottom: Tex.PLANKS_OAK, side: Tex.CRAFTING_TABLE_SIDE });
    
    emitter(62, "furnace_active", { top: Tex.FURNACE_TOP, bottom: Tex.FURNACE_TOP, side: Tex.FURNACE_SIDE }, 13);
    opaque(73, "redstone_ore", { top: Tex.REDSTONE_ORE, bottom: Tex.REDSTONE_ORE, side: Tex.REDSTONE_ORE });
    emitter(89, "glowstone", { top: Tex.SAND, bottom: Tex.SAND, side: Tex.SAND }, 15); // Placeholder texture
  }
}
```


---

### 2. Mob ECS Factory (Data-Oriented Design)

Mobs must not be OOP instances. We define a `MobStats` component to hold static data (width, height, speed) tightly packed in memory. The `MobFactory` allocates an Entity ID and writes directly into the `ComponentStore` SoA arrays, bypassing the garbage collector entirely.

```typescript
// /src/engine/ecs/components/MobStats.ts
import { ComponentDesc } from "../ComponentStore";

/** Static, immutable stats per mob type. Stored contiguously for AI/Physics cache locality. */
export const MobStatsDesc = {
  width:         { type: Float32Array, length: 1 },
  height:        { type: Float32Array, length: 1 },
  eyeHeight:     { type: Float32Array, length: 1 },
  moveSpeed:     { type: Float32Array, length: 1 },
  attackDamage:  { type: Float32Array, length: 1 },
  maxHealth:     { type: Float32Array, length: 1 },
  modelId:       { type: Uint32Array,  length: 1 }, // Maps to VAO ID in MobRenderSystem
} as const satisfies ComponentDesc;

// /src/content/mobs/MobFactory.ts
import { EntityManager } from "../../engine/ecs/EntityManager";
import { ComponentStore } from "../../engine/ecs/ComponentStore";
import { TransformDesc } from "../../engine/ecs/components/Transform";
import { RigidBodyDesc } from "../../engine/ecs/components/RigidBody";
import { HealthDesc } from "../../engine/ecs/components/Health";
import { AIStateDesc } from "../../engine/ecs/components/AIState";
import { MobStatsDesc } from "../../engine/ecs/components/MobStats";
import { HostileTagDesc, FriendlyTagDesc } from "../../engine/ecs/components/Tags";

/** Enum of Mob Types. Used to configure the archetype at spawn time. */
export const enum MobType {
  PIG, COW, SHEEP, CHICKEN, // Friendly
  ZOMBIE, SKELETON, CREEPER, SPIDER, // Hostile
}

interface MobConfig {
  stats: { width: number; height: number; eyeHeight: number; moveSpeed: number; attackDamage: number; maxHealth: number; };
  modelId: number;
  hostile: boolean;
}

/** Config table. O(1) lookup by MobType enum index. */
const MOB_CONFIGS: MobConfig[] = [
  { stats: { width: 0.9, height: 0.9, eyeHeight: 0.8, moveSpeed: 4.0, attackDamage: 0,  maxHealth: 10 }, modelId: 1, hostile: false }, // PIG
  { stats: { width: 0.9, height: 1.2, eyeHeight: 1.1, moveSpeed: 4.0, attackDamage: 0,  maxHealth: 10 }, modelId: 2, hostile: false }, // COW
  { stats: { width: 0.9, height: 1.3, eyeHeight: 1.2, moveSpeed: 4.0, attackDamage: 0,  maxHealth: 8  }, modelId: 3, hostile: false }, // SHEEP
  { stats: { width: 0.6, height: 0.8, eyeHeight: 0.7, moveSpeed: 4.0, attackDamage: 0,  maxHealth: 4  }, modelId: 4, hostile: false }, // CHICKEN
  { stats: { width: 0.6, height: 1.9, eyeHeight: 1.7, moveSpeed: 4.3, attackDamage: 4,  maxHealth: 20 }, modelId: 5, hostile: true  }, // ZOMBIE
  { stats: { width: 0.6, height: 1.9, eyeHeight: 1.7, moveSpeed: 5.0, attackDamage: 4,  maxHealth: 20 }, modelId: 6, hostile: true  }, // SKELETON
  { stats: { width: 0.6, height: 1.7, eyeHeight: 1.5, moveSpeed: 4.5, attackDamage: 10, maxHealth: 20 }, modelId: 7, hostile: true  }, // CREEPER
  { stats: { width: 1.0, height: 0.8, eyeHeight: 0.6, moveSpeed: 6.0, attackDamage: 2,  maxHealth: 16 }, modelId: 8, hostile: true  }, // SPIDER
];

export class MobFactory {
  constructor(
    private readonly em: EntityManager,
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly bodies: ComponentStore<typeof RigidBodyDesc>,
    private readonly healths: ComponentStore<typeof HealthDesc>,
    private readonly aiStates: ComponentStore<typeof AIStateDesc>,
    private readonly mobStats: ComponentStore<typeof MobStatsDesc>,
    private readonly hostileTags: ComponentStore<typeof HostileTagDesc>,
    private readonly friendlyTags: ComponentStore<typeof FriendlyTagDesc>,
  ) {}

  /**
   * Spawns a mob into the ECS world.
   * 
   * O(1) Complexity: Allocates an ID and appends to parallel TypedArrays.
   * Zero allocations: Writes directly into existing pre-allocated buffers.
   */
  spawn(type: MobType, x: number, y: number, z: number): number {
    const entityId = this.em.allocate();
    const entityIdx = entityId & 0x00FF_FFFF; // Unpack index from ID
    const cfg = MOB_CONFIGS[type];

    // --- Transform ---
    const tRow = this.transforms.add(entityIdx);
    const tPos = this.transforms.data.position;
    tPos[tRow * 3 + 0] = x;
    tPos[tRow * 3 + 1] = y;
    tPos[tRow * 3 + 2] = z;
    // rotation = [0,0,0,1] (identity quat)
    this.transforms.data.rotation[tRow * 4 + 3] = 1.0; 
    this.transforms.data.scale[tRow * 3 + 0] = 1.0;
    this.transforms.data.scale[tRow * 3 + 1] = 1.0;
    this.transforms.data.scale[tRow * 3 + 2] = 1.0;

    // --- RigidBody (Physics AABB derived from stats) ---
    const bRow = this.bodies.add(entityIdx);
    const bMin = this.bodies.data.aabbMin;
    const bMax = this.bodies.data.aabbMax;
    // Centered AABB: min(-w/2, 0, -w/2) to max(w/2, h, w/2)
    const halfW = cfg.stats.width / 2;
    bMin[bRow * 3 + 0] = -halfW; bMin[bRow * 3 + 1] = 0;          bMin[bRow * 3 + 2] = -halfW;
    bMax[bRow * 3 + 0] =  halfW; bMax[bRow * 3 + 1] = cfg.stats.height; bMax[bRow * 3 + 2] =  halfW;
    this.bodies.data.gravity[bRow] = 20.0; // 20 blocks/s^2
    this.bodies.data.onGround[bRow] = 0;

    // --- Mob Stats ---
    const sRow = this.mobStats.add(entityIdx);
    const s = cfg.stats;
    const sd = this.mobStats.data;
    sd.width[sRow] = s.width; sd.height[sRow] = s.height; sd.eyeHeight[sRow] = s.eyeHeight;
    sd.moveSpeed[sRow] = s.moveSpeed; sd.attackDamage[sRow] = s.attackDamage;
    sd.maxHealth[sRow] = s.maxHealth; sd.modelId[sRow] = cfg.modelId;

    // --- Health ---
    const hRow = this.healths.add(entityIdx);
    this.healths.data.hp[hRow] = s.maxHealth;
    this.healths.data.maxHp[hRow] = s.maxHealth;
    this.healths.data.regenCd[hRow] = 0.0;

    // --- AI State ---
    const aRow = this.aiStates.add(entityIdx);
    this.aiStates.data.state[aRow] = 0; // 0 = Idle
    this.aiStates.data.attackCd[aRow] = 0.0;
    this.aiStates.data.targetEntity[aRow] = 0xFFFFFFFF; // Invalid target ID

    // --- Archetype Tag (Sparse Set insertion) ---
    if (cfg.hostile) {
      this.hostileTags.add(entityIdx);
    } else {
      this.friendlyTags.add(entityIdx);
    }

    return entityId;
  }
}
```

### Summary of Architecture Compliance


1. **Strict DOD**: Mobs are not `class Pig extends Mob`. They are integer Entity IDs. Data like hitpoints, position, and AABB size is shattered into parallel `Float32Array`s via `ComponentStore`. The CPU prefetcher perfectly streams this memory during `PhysicsSystem` and `AISystem` updates.
2. **No GC Pressure**: The `MobFactory.spawn` method mutates pre-allocated arrays. The only allocation is the integer ID from the EntityManager's recycled free-list.
3. **Abstract Factory Pattern**: Blocks use a `MaterialFactory` interface that decouples content registration (`VanillaBlockFactory`) from the `MaterialRegistry` engine core.
4. **Partial AABBs**: Demonstrated by the Cactus block and the centered mob collision hulls, enabling precise swept-AABB physics without OOP vector objects.


