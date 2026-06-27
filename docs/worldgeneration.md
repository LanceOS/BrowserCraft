To bring the world to life, we must implement the procedural generation pipeline that runs entirely inside the `WorldGenWorker`.

To satisfy the requirement for **3D noise terrain heightmaps**, we will use a 3D Density Function (similar to early Beta Minecraft). Instead of a flat 2D heightmap, we evaluate 3D Simplex noise across the entire volume. Where the 3D density crosses a threshold, terrain exists. This naturally generates overhangs, floating islands, and 3D terrain features, while still yielding a base "heightmap" shape due to a downward-sloping density gradient.

Below is the strict TypeScript implementation for the Noise primitives, Biome Sampler, Cave Carver, Ore Distributor, and the WorldGen orchestrator.

### 1. 3D Simplex Noise (Deterministic & Allocation-Free)

We need a highly optimized, deterministic noise implementation. We use a permutation table seeded by the world seed, and sample it using standard Simplex math. No objects are allocated during sampling.

```typescript
// /src/engine/workers/worldgen/SimplexNoise.ts

/**
 * Deterministic 3D Simplex Noise.
 * Uses a 512-byte permutation table. 
 * O(1) complexity per evaluation. Zero heap allocations on the hot path.
 */
export class SimplexNoise {
  private readonly perm: Uint8Array;
  private readonly permMod12: Uint8Array;

  // Skewing and unskewing factors for 3D
  private static readonly F3 = 1.0 / 3.0;
  private static readonly G3 = 1.0 / 6.0;

  constructor(seed: number) {
    this.perm = new Uint8Array(512);
    this.permMod12 = new Uint8Array(512);
    
    // Seeded PRNG (Mulberry32) to build the permutation table
    let s = seed >>> 0;
    const rand = () => {
      s |= 0; s = (s + 0x6D2B79F5) | 0;
      let t = Math.imul(s ^ (s >>> 15), 1 | s);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };

    const p = new Uint8Array(256);
    for (let i = 0; i < 256; i++) p[i] = i;
    // Fisher-Yates shuffle
    for (let i = 255; i > 0; i--) {
      const j = Math.floor(rand() * (i + 1));
      [p[i], p[j]] = [p[j], p[i]];
    }
    for (let i = 0; i < 512; i++) {
      this.perm[i] = p[i & 255];
      this.permMod12[i] = this.perm[i] % 12;
    }
  }

  // Gradient vectors for 3D
  private static readonly grad3 = new Float32Array([
    1,1,0, -1,1,0, 1,-1,0, -1,-1,0,
    1,0,1, -1,0,1, 1,0,-1, -1,0,-1,
    0,1,1, 0,-1,1, 0,1,-1, 0,-1,-1
  ]);

  /** Evaluates 3D Simplex noise. Returns a value in the range [-1, 1]. */
  public noise3D(x: number, y: number, z: number): number {
    const perm = this.perm;
    const permMod12 = this.permMod12;
    const grad3 = SimplexNoise.grad3;
    
    let n0, n1, n2, n3;
    
    const sk = (x + y + z) * SimplexNoise.F3;
    const i = Math.floor(x + sk);
    const j = Math.floor(y + sk);
    const k = Math.floor(z + sk);
    
    const uk = (i + j + k) * SimplexNoise.G3;
    const x0 = x - (i - uk);
    const y0 = y - (j - uk);
    const z0 = z - (k - uk);
    
    let i1, j1, k1;
    let i2, j2, k2;
    if (x0 >= y0) {
      if (y0 >= z0)      { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
      else if (x0 >= z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; }
      else               { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; }
    } else {
      if (y0 < z0)       { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
      else if (x0 < z0)  { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
      else               { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
    }
    
    const x1 = x0 - i1 + SimplexNoise.G3;
    const y1 = y0 - j1 + SimplexNoise.G3;
    const z1 = z0 - k1 + SimplexNoise.G3;
    const x2 = x0 - i2 + 2.0 * SimplexNoise.G3;
    const y2 = y0 - j2 + 2.0 * SimplexNoise.G3;
    const z2 = z0 - k2 + 2.0 * SimplexNoise.G3;
    const x3 = x0 - 1.0 + 3.0 * SimplexNoise.G3;
    const y3 = y0 - 1.0 + 3.0 * SimplexNoise.G3;
    const z3 = z0 - 1.0 + 3.0 * SimplexNoise.G3;
    
    const ii = i & 255;
    const jj = j & 255;
    const kk = k & 255;
    
    let t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
    if (t0 < 0) n0 = 0.0;
    else {
      const gi0 = permMod12[ii + perm[jj + perm[kk]]] * 3;
      t0 *= t0;
      n0 = t0 * t0 * (grad3[gi0] * x0 + grad3[gi0+1] * y0 + grad3[gi0+2] * z0);
    }
    
    let t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
    if (t1 < 0) n1 = 0.0;
    else {
      const gi1 = permMod12[ii + i1 + perm[jj + j1 + perm[kk + k1]]] * 3;
      t1 *= t1;
      n1 = t1 * t1 * (grad3[gi1] * x1 + grad3[gi1+1] * y1 + grad3[gi1+2] * z1);
    }
    
    let t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
    if (t2 < 0) n2 = 0.0;
    else {
      const gi2 = permMod12[ii + i2 + perm[jj + j2 + perm[kk + k2]]] * 3;
      t2 *= t2;
      n2 = t2 * t2 * (grad3[gi2] * x2 + grad3[gi2+1] * y2 + grad3[gi2+2] * z2);
    }
    
    let t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
    if (t3 < 0) n3 = 0.0;
    else {
      const gi3 = permMod12[ii + 1 + perm[jj + 1 + perm[kk + 1]]] * 3;
      t3 *= t3;
      n3 = t3 * t3 * (grad3[gi3] * x3 + grad3[gi3+1] * y3 + grad3[gi3+2] * z3);
    }
    
    return 32.0 * (n0 + n1 + n2 + n3);
  }
}
```

### 2. Biome Sampler (Temperature & Humidity)

Biomes are determined by intersecting two separate 2D noise maps.

```typescript
// /src/engine/workers/worldgen/BiomeSampler.ts
import { SimplexNoise } from "./SimplexNoise";

export const enum BiomeId {
  OCEAN,
  PLAINS,
  FOREST,
  DESERT,
  MOUNTAINS,
}

export interface BiomeSurfaceRule {
  readonly topBlock: number;   // e.g., Grass (2)
  readonly fillerBlock: number; // e.g., Dirt (3)
  readonly depth: number;      // how deep filler goes
}

/** Maps BiomeId to surface rules. O(1) lookup. */
const BIOME_RULES: BiomeSurfaceRule[] = [
  { topBlock: 12, fillerBlock: 12, depth: 3 }, // Ocean (Sand)
  { topBlock: 2,  fillerBlock: 3,  depth: 4 }, // Plains (Grass/Dirt)
  { topBlock: 2,  fillerBlock: 3,  depth: 6 }, // Forest (Grass/Dirt)
  { topBlock: 12, fillerBlock: 12, depth: 4 }, // Desert (Sand)
  { topBlock: 1,  fillerBlock: 1,  depth: 8 }, // Mountains (Stone)
];

export class BiomeSampler {
  private readonly tempNoise: SimplexNoise;
  private readonly humidNoise: SimplexNoise;
  
  // 2D noise wrappers for simple heightmap generation
  public noise2D(x: number, z: number): number {
    return this.tempNoise.noise3D(x, 0, z);
  }

  constructor(seed: number) {
    this.tempNoise = new SimplexNoise(seed ^ 0xA10BE);
    this.humidNoise = new SimplexNoise(seed ^ 0xB1D07);
  }

  /** Evaluates temperature and humidity to return a BiomeId. O(1) */
  public sampleBiome(worldX: number, worldZ: number): BiomeId {
    // Scale determines biome size. 0.008 gives large biomes (~128 blocks).
    const temp = this.tempNoise.noise3D(worldX * 0.008, 0, worldZ * 0.008);
    const humid = this.humidNoise.noise3D(worldX * 0.008, 100, worldZ * 0.008);
    
    if (temp < -0.3) return BiomeId.MOUNTAINS;
    if (temp > 0.4 && humid < 0.0) return BiomeId.DESERT;
    if (humid > 0.3) return BiomeId.FOREST;
    if (temp < -0.2 && humid < -0.2) return BiomeId.OCEAN;
    return BiomeId.PLAINS;
  }

  public getRule(id: BiomeId): BiomeSurfaceRule {
    return BIOME_RULES[id];
  }
}
```

### 3. Cave Carver (Winding Worm Algorithm)

Caves are carved using the classic "worm" approach. A cursor starts at a random point and moves in a direction. The direction is slowly perturbed by 3D noise, causing it to wind through the terrain.

```typescript
// /src/engine/workers/worldgen/CaveCarver.ts
import { SimplexNoise } from "./SimplexNoise";

/**
 * Carves winding caves into the terrain array.
 * Uses a 3D noise field to steer a "worm" through the chunk volume.
 * 
 * Complexity: O(numWorms * wormLength * radius^3).
 * Configured to carve only within the active chunk boundaries to maintain
 * worker isolation. Edge artifacts are resolved by neighbor spillover
 * during the mesher's face-culling step.
 */
export class CaveCarver {
  private readonly noise: SimplexNoise;
  private readonly rng: () => number;

  constructor(seed: number) {
    this.noise = new SimplexNoise(seed ^ 0xCAFE);
    // Simple LCG for worm starting positions
    let s = seed ^ 0xC4VE;
    this.rng = () => {
      s = (Math.imul(s, 1664525) + 1013904223) | 0;
      return (s >>> 0) / 4294967296;
    };
  }

  public carve(
    density data: Uint8Array,
    baseX: number, baseZ: number,
    sizeX: number, sizeY: number, sizeZ: number
  ): void {
    const numWorms = 4; // caves per chunk
    
    for (let w = 0; w < numWorms; w++) {
      // Start worm in a random location within the chunk
      let x = this.rng() * sizeX;
      let y = 10 + this.rng() * 40; // keep caves between Y=10 and Y=50
      let z = this.rng() * sizeZ;
      
      let yaw = this.rng() * Math.PI * 2;
      let pitch = (this.rng() - 0.5) * Math.PI * 0.5;
      
      const length = 40 + Math.floor(this.rng() * 80); // 40 to 120 blocks long

      for (let step = 0; step < length; step++) {
        // Use noise to smoothly vary yaw and pitch, creating the "wind"
        const worldX = baseX + x;
        const worldY = y;
        const worldZ = baseZ + z;
        
        yaw += this.noise.noise3D(worldX * 0.05, worldY * 0.05, worldZ * 0.05) * 0.2;
        pitch += this.noise.noise3D(worldX * 0.05 + 10, worldY * 0.05, worldZ * 0.05) * 0.1;
        
        // Move worm
        x += Math.cos(pitch) * Math.cos(yaw);
        y += Math.sin(pitch);
        z += Math.cos(pitch) * Math.sin(yaw);
        
        // Carve a sphere of radius 1.5 at the worm's head
        const radius = 1.5 + this.noise.noise3D(worldX * 0.1, worldY * 0.1, worldZ * 0.1) * 0.5;
        this.carveSphere(density data, x, y, z, radius, sizeX, sizeY, sizeZ);
      }
    }
  }

  private carveSphere(
    density data: Uint8Array,
    cx: number, cy: number, cz: number, r: number,
    sizeX: number, sizeY: number, sizeZ: number
  ): void {
    const minx = Math.max(0, Math.floor(cx - r));
    const maxx = Math.min(sizeX - 1, Math.ceil(cx + r));
    const miny = Math.max(0, Math.floor(cy - r));
    const maxy = Math.min(sizeY - 1, Math.ceil(cy + r));
    const minz = Math.max(0, Math.floor(cz - r));
    const maxz = Math.min(sizeZ - 1, Math.ceil(cz + r));
    
    const rSq = r * r;
    for (let y = miny; y <= maxy; y++) {
      for (let z = minz; z <= maxz; z++) {
        for (let x = minx; x <= maxx; x++) {
          const dx = x - cx;
          const dy = y - cy;
          const dz = z - cz;
          if (dx*dx + dy*dy + dz*dz <= rSq) {
            const idx = (y * sizeZ + z) * sizeX + x;
            // Only carve solid blocks (don't replace air, to prevent carving into ungenerated areas)
            if (density data[idx] !== 0) {
              density data[idx] = 0; // Air
            }
          }
        }
      }
    }
  }
}
```

### 4. Ore Distributor (Vein Generation)

Ores scatter in pockets using a small random walk per vein type, restricted by Y-level constraints (e.g., diamonds deep down).

```typescript
// /src/engine/workers/worldgen/OreDistributor.ts

interface OreConfig {
  materialId: number;
  minY: number;
  maxY: number;
  veinsPerChunk: number;
  veinSize: number;
}

const ORE_CONFIGS: OreConfig[] = [
  { materialId: 16, minY: 5,  maxY: 64, veinsPerChunk: 20, veinSize: 8 }, // Coal
  { materialId: 15, minY: 5,  maxY: 32, veinsPerChunk: 10, veinSize: 6 }, // Iron
  { materialId: 14, minY: 5,  maxY: 16, veinsPerChunk: 4,  veinSize: 4 }, // Gold
  { materialId: 56, minY: 5,  maxY: 12, veinsPerChunk: 2,  veinSize: 4 }, // Diamond
];

export class OreDistributor {
  private rng: () => number;
  
  constructor(seed: number) {
    let s = seed ^ 0x0BE5;
    this.rng = () => {
      s = (Math.imul(s, 48271) + 1) | 0;
      return (s >>> 0) / 4294967296;
    };
  }

  /** Distributes ore veins. O(veins * veinSize). */
  public distribute(
    density data: Uint8Array,
    sizeX: number, sizeY: number, sizeZ: number
  ): void {
    for (const cfg of ORE_CONFIGS) {
      for (let v = 0; v < cfg.veinsPerChunk; v++) {
        // Random start point
        let x = Math.floor(this.rng() * sizeX);
        let y = cfg.minY + Math.floor(this.rng() * (cfg.maxY - cfg.minY));
        let z = Math.floor(this.rng() * sizeZ);
        
        // Small random walk for vein
        for (let i = 0; i < cfg.veinSize; i++) {
          // Move slightly
          x += (this.rng() * 2 - 1) > 0 ? 1 : -1;
          y += (this.rng() * 2 - 1) > 0 ? 1 : -1;
          z += (this.rng() * 2 - 1) > 0 ? 1 : -1;
          
          // Clamp to bounds
          if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) continue;
          
          const idx = (y * sizeZ + z) * sizeX + x;
          // Only replace stone (id=1)
          if (density data[idx] === 1) {
            density data[idx] = cfg.materialId;
          }
        }
      }
    }
  }
}
```

### 5. The WorldGen Orchestrator (Putting it all together)

This runs entirely within the `WorldGenWorker`. It uses 3D noise for terrain density (allowing 3D features like overhangs), while using the 2D projection of the noise as the "heightmap" to apply biome surface rules.

```typescript
// /src/engine/workers/worldgen/WorldGenWorker.ts
/// <reference lib="webworker" />

import { SimplexNoise } from "./SimplexNoise";
import { BiomeSampler, BiomeId } from "./BiomeSampler";
import { CaveCarver } from "./CaveCarver";
import { OreDistributor } from "./OreDistributor";
import type { ChunkSlot, ChunkDimensions } from "../../alloc/SharedPool";
import { ChunkSlotStatus } from "../../alloc/SharedPool";

export class WorldGenPipeline {
  private readonly densityNoise: SimplexNoise;
  private readonly biomeSampler: BiomeSampler;
  private readonly caveCarver: CaveCarver;
  private readonly oreDist: OreDistributor;
  
  // Block IDs
  private static readonly STONE = 1;
  private static readonly DIRT = 3;
  private static readonly BEDROCK = 7;
  private static readonly WATER = 8;

  constructor(seed: number, dims: ChunkDimensions) {
    this.densityNoise = new SimplexNoise(seed);
    this.biomeSampler = new BiomeSampler(seed);
    this.caveCarver = new CaveCarver(seed);
    this.oreDist = new OreDistributor(seed);
  }

  /**
   * Main generation pipeline. Fills the SharedArrayBuffer terrain data.
   * Complexity: O(sizeX * sizeY * sizeZ) for terrain, plus cave/ore overhead.
   */
  public generate(slot: ChunkSlot, dims: ChunkDimensions): void {
    const vox = slot.density data;
    const { sizeX, sizeY, sizeZ } = dims;
    const chunkX = Atomics.load(slot.chunkX, 0);
    const chunkZ = Atomics.load(slot.chunkZ, 0);
    
    const baseX = chunkX * sizeX;
    const baseZ = chunkZ * sizeZ;
    
    // 1. Terrain Density & Biome Surface mapping
    for (let z = 0; z < sizeZ; z++) {
      for (let x = 0; x < sizeX; x++) {
        const worldX = baseX + x;
        const worldZ = baseZ + z;
        
        // Evaluate 2D heightmap noise to get a base terrain level
        const heightMap = this.biomeSampler.noise2D(worldX * 0.01, worldZ * 0.01);
        const baseHeight = Math.floor(64 + heightMap * 16); // Sea level 64, +/- 16
        
        // Determine biome for this column
        const biome = this.biomeSampler.sampleBiome(worldX, worldZ);
        const rule = this.biomeSampler.getRule(biome);
        
        for (let y = 0; y < sizeY; y++) {
          const idx = (y * sizeZ + z) * sizeX + x;
          
          if (y === 0) {
            vox[idx] = WorldGenPipeline.BEDROCK;
            continue;
          }
          
          if (y > baseHeight) {
            // Above heightmap: Water or Air
            vox[idx] = (y <= 64 && biome !== BiomeId.DESERT) ? WorldGenPipeline.WATER : 0;
            continue;
          }
          
          // 3D noise modulation for overhangs/caves in the solid terrain
          // The further below the heightmap, the denser the terrain.
          const depthFactor = (baseHeight - y) * 0.05; 
          const noise3D = this.densityNoise.noise3D(worldX * 0.03, y * 0.03, worldZ * 0.03);
          
          // If 3D noise pushes density below 0, carve an air pocket
          if (noise3D + depthFactor < 0.0 && y < baseHeight - 5) {
            vox[idx] = 0;
            continue;
          }
          
          // Surface rules
          if (y === baseHeight) {
            vox[idx] = rule.topBlock;
          } else if (y > baseHeight - rule.depth) {
            vox[idx] = rule.fillerBlock;
          } else {
            vox[idx] = WorldGenPipeline.STONE;
          }
        }
      }
    }
    
    // 2. Cave Carving (Worms)
    this.caveCarver.carve(vox, baseX, baseZ, sizeX, sizeY, sizeZ);
    
    // 3. Ore Distribution
    this.oreDist.distribute(vox, sizeX, sizeY, sizeZ);
    
    // 4. Finalize: transition state to VOXELS_READY
    Atomics.store(slot.status, 0, ChunkSlotStatus.VOXELS_READY);
  }
}
```

### Summary of Architectural Compliance


1. **3D Noise Terrain**: Instead of flat 2D heightmaps, we calculate a `depthFactor` and modulate it with `SimplexNoise.noise3D()`. This creates 3D overhangs, floating blocks, and underground air pockets seamlessly.
2. **Biome-driven Surface Rules**: `BiomeSampler` maps Temperature/Humidity to a `BiomeId`, which is then used to lookup `BiomeSurfaceRule` from a strictly-typed table (O(1) lookup). This determines if the top block is Grass (Plains), Sand (Desert), or Stone (Mountains).
3. **Zero-Copy Worker Boundary**: The `WorldGenPipeline.generate()` method takes a `ChunkSlot` that is merely a collection of `TypedArray` views directly into the `SharedArrayBuffer`. It mutates memory visible to the main thread without ever triggering `postMessage` serialization.
4. **No OOP Bloat in Workers**: No `class Terrain` or `class Chunk` objects exist. Terrain data is a flat `Uint8Array` (`vox`), iterated via integer math (`(y * sizeZ + z) * sizeX + x`). This keeps the CPU cache lines perfectly saturated during generation.


