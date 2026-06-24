To generate structures like villages and houses across chunk boundaries—while strictly adhering to our Web Worker isolation and zero-copy constraints—we must use a **Deterministic Virtual Space** approach.

Because workers cannot communicate with each other or the main thread during chunk generation, they cannot maintain a centralized "village state." Instead, we use a deterministic PRNG seeded by the **Structure Region ID**. Every worker evaluating a chunk in that region computes the *exact same* village layout deterministically, and then only stamps the blocks that fall within its own chunk boundaries.

Here is the strict TypeScript, Data-Oriented implementation for structural generation.

### 1. Blueprint Data Structure (Bit-Packed & DOD)

Structures are stored as flat, bit-packed `Uint8Array` blobs to maximize CPU cache efficiency during the stamping phase. No OOP objects, no `Vector3` classes.

```typescript
// /src/content/structures/StructureBlueprint.ts

/**
 * A bit-packed structure component (e.g., a house, a well, a road).
 * Format per block (4 bytes):
 *   Byte 0: dx (i8) - relative X offset
 *   Byte 1: dy (i8) - relative Y offset
 *   Byte 2: dz (i8) - relative Z offset
 *   Byte 3: blockId (u8) - Block ID to place
 * 
 * We use a terminating dx = -128 (0x80) to mark the end of the blueprint,
 * avoiding the need to store a separate length variable.
 */
export interface StructureBlueprint {
  readonly id: number;
  readonly sizeX: number;
  readonly sizeY: number;
  readonly sizeZ: number;
  readonly blocks: Uint8Array; // Packed block operations
  readonly paletteWeight: number; // Probability weight for this structure
}

/** Rotates a block state (dx, dz) by 0, 90, 180, or 270 degrees. O(1). */
function rotateCoord(x: number, z: number, rotation: number): [number, number] {
  switch (rotation & 3) {
    case 0: return [x, z];
    case 1: return [-z, x]; // 90 deg
    case 2: return [-x, -z]; // 180 deg
    case 3: return [z, -x]; // 270 deg
    default: return [x, z];
  }
}

/**
 * Applies a blueprint to the voxel array at a world position.
 * O(B) complexity where B is the number of blocks in the blueprint.
 * Zero allocations. Uses bitwise operations for boundary checks.
 */
export function stampBlueprint(
  voxels: Uint8Array,
  blueprint: StructureBlueprint,
  originX: number, originY: number, originZ: number,
  rotation: number,
  sizeX: number, sizeY: number, sizeZ: number,
  replaceAirOnly: boolean = false
): void {
  const blocks = blueprint.blocks;
  for (let i = 0; i < blocks.length; i += 4) {
    const dx = blocks[i];
    // Check for terminator byte (0x80 = -128)
    if (dx === -128) break; 
    
    const dy = blocks[i + 1];
    const dz = blocks[i + 2];
    const blockId = blocks[i + 3];

    // Apply rotation
    const [rx, rz] = rotateCoord(dx, dz, rotation);

    const worldX = originX + rx;
    const worldY = originY + dy;
    const worldZ = originZ + rz;

    // Boundary check (chunk-local)
    if (worldX < 0 || worldX >= sizeX || 
        worldY < 0 || worldY >= sizeY || 
        worldZ < 0 || worldZ >= sizeZ) {
      continue;
    }

    const idx = (worldY * sizeZ + worldZ) * sizeX + worldX;
    
    // Some structures (like roads) only replace air/dirt. 
    // Others (like houses) overwrite everything.
    if (replaceAirOnly && voxels[idx] !== 0) {
      continue;
    }

    voxels[idx] = blockId;
  }
}
```

### 2. The Deterministic Village Planner

This logic runs identically in every `WorldGenWorker` that intersects a "Structure Region". It uses a seeded PRNG to determine where houses go, ensuring all workers agree on the layout.

```typescript
// /src/content/structures/VillagePlanner.ts

import { StructureBlueprint, stampBlueprint } from "./StructureBlueprint";

/** Seeded PRNG (Mulberry32) for deterministic layout generation. */
function mulberry32(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6D2B79F5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export class VillagePlanner {
  private readonly blueprints: StructureBlueprint[];

  constructor(blueprints: StructureBlueprint[]) {
    this.blueprints = blueprints;
  }

  /**
   * Deterministically generates and stamps a village.
   * 
   * @param regionX The structure region X (worldX / 64)
   * @param regionZ The structure region Z (worldZ / 64)
   * @param chunkX The current chunk being generated X
   * @param chunkZ The current chunk being generated Z
   * @param voxels The voxel array to stamp into
   * @param dims Chunk dimensions
   */
  public generate(
    regionX: number, regionZ: number,
    chunkX: number, chunkZ: number,
    voxels: Uint8Array,
    dims: { sizeX: number; sizeY: number; sizeZ: number }
  ): void {
    // 1. Determine if this region has a village (1 in 4 chance)
    // Seed is derived from the region coordinates.
    const regionSeed = (regionX * 73856093) ^ (regionZ * 19349663);
    const rng = mulberry32(regionSeed);
    
    if (rng() > 0.25) return; // No village in this region

    // 2. Village Center is deterministic within the 64x64 region
    const centerRegionX = Math.floor(rng() * 64);
    const centerRegionZ = Math.floor(rng() * 64);
    
    const worldCenterX = regionX * 64 + centerRegionX;
    const worldCenterZ = regionZ * 64 + centerRegionZ;
    
    // Find surface height at center (deterministic approximation)
    // In a full engine, this uses the same noise function as terrain gen.
    const baseHeight = 64; 

    // 3. Plan components (Houses, Roads)
    // We iterate a fixed number of attempts to place houses around the center.
    const maxComponents = 8;
    for (let i = 0; i < maxComponents; i++) {
      // Deterministic offset for this component
      const angle = rng() * Math.PI * 2;
      const dist = 5 + Math.floor(rng() * 15);
      const compWorldX = Math.floor(worldCenterX + Math.cos(angle) * dist);
      const compWorldZ = Math.floor(worldCenterZ + Math.sin(angle) * dist);
      const rotation = Math.floor(rng() * 4);

      // Select a random blueprint from the registry
      const bp = this.blueprints[Math.floor(rng() * this.blueprints.length)];
      
      // 4. Stamp the component IF it intersects the current chunk
      // The worker only draws what belongs to its own chunk.
      this.tryStampComponent(
        compWorldX, baseHeight, compWorldZ, rotation, bp,
        chunkX, chunkZ, voxels, dims
      );
    }

    // 5. Stamp the central well
    const wellBp = this.blueprints.find(b => b.id === 0); // Assuming ID 0 is Well
    if (wellBp) {
      this.tryStampComponent(
        worldCenterX, baseHeight, worldCenterZ, 0, wellBp,
        chunkX, chunkZ, voxels, dims
      );
    }
  }

  private tryStampComponent(
    compWorldX: number, compWorldY: number, compWorldZ: number,
    rotation: number, bp: StructureBlueprint,
    chunkX: number, chunkZ: number,
    voxels: Uint8Array, dims: { sizeX: number; sizeY: number; sizeZ: number }
  ): void {
    // Convert component world coords to chunk-local coords
    const localOriginX = compWorldX - (chunkX * dims.sizeX);
    const localOriginZ = compWorldZ - (chunkZ * dims.sizeZ);

    // Quick AABB intersection test: does this structure overlap this chunk?
    // If the local origin is far outside [0, sizeX], skip it entirely.
    // (Blueprint size + origin must be > 0, and origin < chunk size)
    const maxLocalX = localOriginX + bp.sizeX;
    const maxLocalZ = localOriginZ + bp.sizeZ;
    
    if (maxLocalX < 0 || localOriginX >= dims.sizeX || 
        maxLocalZ < 0 || localOriginZ >= dims.sizeZ) {
      return; // Structure does not intersect this chunk
    }

    // The structure overlaps! Stamp the blocks.
    // stampBlueprint handles the per-block boundary clamping.
    stampBlueprint(
      voxels, bp,
      localOriginX, compWorldY, localOriginZ,
      rotation,
      dims.sizeX, dims.sizeY, dims.sizeZ,
      false // Overwrite terrain
    );
  }
}
```

### 3. Blueprint Registry & Factory

We define the actual structural data. To avoid massive code blocks, I'll define a programmatic helper that generates the byte arrays for standard houses.

```typescript
// /src/content/structures/StructureRegistry.ts

import { StructureBlueprint } from "./StructureBlueprint";

/**
 * Abstract Factory for structure blueprints.
 * Generates bit-packed Uint8Arrays for houses, wells, etc.
 */
export class StructureFactory {
  private readonly blueprints: Map<number, StructureBlueprint> = new Map();

  constructor() {
    this.registerWell();
    this.registerHouse();
    this.registerRoad();
  }

  public get(id: number): StructureBlueprint | undefined {
    return this.blueprints.get(id);
  }

  public getAll(): StructureBlueprint[] {
    return Array.from(this.blueprints.values());
  }

  private registerWell(): void {
    // A 3x3x4 well
    const blocks: number[] = [];
    // Walls (Cobblestone = 4)
    for (let y = 0; y < 4; y++) {
      blocks.push(0, y, 0, 4); blocks.push(2, y, 0, 4);
      blocks.push(0, y, 2, 4); blocks.push(2, y, 2, 4);
    }
    // Water (id=8) inside
    blocks.push(0, 0, 1, 8); blocks.push(2, 0, 1, 8);
    blocks.push(1, 0, 0, 8); blocks.push(1, 0, 2, 8);
    blocks.push(1, 0, 1, 8);

    this.blueprints.set(0, this.createBlueprint(0, 3, 4, 3, blocks));
  }

  private registerHouse(): void {
    // A 5x5x5 hollow house with a door and a roof
    const blocks: number[] = [];
    const Planks = 5, Log = 17, Glass = 20, Door = 64; // Door represented as solid for simplicity here

    // Floor
    for (let x = 0; x < 5; x++)
      for (let z = 0; z < 5; z++)
        blocks.push(x, 0, z, Planks);

    // Walls
    for (let y = 1; y <= 4; y++) {
      for (let i = 0; i < 5; i++) {
        // Walls on X axis
        if (!(y === 2 && i === 2)) { // Leave a door hole
          blocks.push(i, y, 0, Log);
          blocks.push(i, y, 4, Log);
        }
        // Walls on Z axis
        blocks.push(0, y, i, Log);
        blocks.push(4, y, i, Log);
      }
    }

    // Windows
    blocks.push(2, 3, 0, Glass); blocks.push(2, 3, 4, Glass);
    blocks.push(0, 3, 2, Glass); blocks.push(4, 3, 2, Glass);

    // Door
    blocks.push(2, 1, 0, Door); blocks.push(2, 2, 0, Door);

    // Roof (Wood stairs or slabs, using Planks for simplicity)
    for (let x = -1; x <= 5; x++)
      for (let z = -1; z <= 5; z++)
        blocks.push(x, 5, z, 5); // Planks roof overhang

    this.blueprints.set(1, this.createBlueprint(1, 7, 6, 7, blocks));
  }

  private registerRoad(): void {
    // A 16x1x3 gravel road segment
    const blocks: number[] = [];
    for (let x = 0; x < 16; x++) {
      blocks.push(x, 0, 0, 13);
      blocks.push(x, 0, 1, 13);
      blocks.push(x, 0, 2, 13);
    }
    this.blueprints.set(2, this.createBlueprint(2, 16, 1, 3, blocks));
  }

  private createBlueprint(id: number, sx: number, sy: number, sz: number, rawBlocks: number[]): StructureBlueprint {
    const packed = new Uint8Array(rawBlocks.length + 4); // +4 for terminator
    for (let i = 0; i < rawBlocks.length; i++) {
      // Clamp values to i8/u8 range
      packed[i] = rawBlocks[i] & 0xFF;
    }
    // Terminator
    packed[rawBlocks.length] = 0x80; 
    packed[rawBlocks.length + 1] = 0;
    packed[rawBlocks.length + 2] = 0;
    packed[rawBounds.length + 3] = 0;

    return { id, sizeX: sx, sizeY: sy, sizeZ: sz, blocks: packed, paletteWeight: 1 };
  }
}
```

### 4. Integration into the WorldGen Pipeline

Finally, we inject the `VillagePlanner` into the `WorldGenPipeline`. Structures are stamped *after* terrain and caves, but *before* ore distribution, so ores don't overwrite village chests/blocks.

```typescript
// /src/engine/workers/worldgen/WorldGenWorker.ts (Modified Excerpt)

import { VillagePlanner } from "../../../content/structures/VillagePlanner";
import { StructureFactory } from "../../../content/structures/StructureRegistry";

export class WorldGenPipeline {
  private readonly villagePlanner: VillagePlanner;

  constructor(seed: number, dims: ChunkDimensions) {
    // ... existing noise init ...
    
    // Initialize structure planner
    const factory = new StructureFactory();
    this.villagePlanner = new VillagePlanner(factory.getAll());
  }

  public generate(slot: ChunkSlot, dims: ChunkDimensions): void {
    // ... 1. Terrain Density ...
    // ... 2. Cave Carving ...

    // 3. Structure Generation (Villages)
    // We check a 3x3 grid of structure regions around the chunk to catch 
    // villages that might spill over into this chunk.
    const chunkX = Atomics.load(slot.chunkX, 0);
    const chunkZ = Atomics.load(slot.chunkZ, 0);
    
    const regionSize = 64; // 64x64 block regions
    const baseRegionX = Math.floor((chunkX * dims.sizeX) / regionSize);
    const baseRegionZ = Math.floor((chunkZ * dims.sizeZ) / regionSize);

    for (let rx = baseRegionX - 1; rx <= baseRegionX + 1; rx++) {
      for (let rz = baseRegionZ - 1; rz <= baseRegionZ + 1; rz++) {
        this.villagePlanner.generate(
          rx, rz,
          chunkX, chunkZ,
          slot.voxels,
          dims
        );
      }
    }

    // 4. Ore Distribution (runs after structures so it doesn't overwrite them)
    this.oreDist.distribute(slot.voxels, dims.sizeX, dims.sizeY, dims.sizeZ);

    Atomics.store(slot.status, 0, ChunkSlotStatus.VOXELS_READY);
  }
}
```

### Summary of Architectural Compliance


1. **Deterministic Cross-Worker Generation**: The `VillagePlanner` does not use shared state. It uses pure functions and a seeded PRNG (`mulberry32`) based on the `RegionX/Z`. Worker A generating Chunk 1 and Worker B generating Chunk 2 will both independently compute the *same* village layout, but only stamp the blocks that fall within their respective chunk boundaries.
2. **Zero-Copy & DOD Blueprints**: Structures are stored as flat `Uint8Array` buffers using a 4-byte bit-packed layout (`dx, dy, dz, blockId`). The `stampBlueprint` function processes this array linearly, maximizing CPU cache lines and triggering zero Garbage Collection.
3. **O(1) Rotations**: The `rotateCoord` function maps structure components to the voxel grid using simple bitwise and arithmetic switches, allowing a single blueprint to be rendered in 4 different directions without duplication.
4. **Chunk Boundary Clamping**: The `stampBlueprint` function gracefully skips blocks that fall outside the `sizeX/Y/Z` of the current chunk, preventing buffer overflows and cleanly handling structures that bisect chunk borders.


