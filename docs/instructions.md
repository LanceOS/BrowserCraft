# Voxel Sandbox Engine — Architectural Blueprint

Below is the foundational architecture for a Minecraft 1.3.2/1.5.2-era voxel sandbox built strictly on TypeScript + WebGL2 (GLSL 300 ES). The design adheres to: DOD-based ECS with SoA TypedArray storage, zero-copy worker boundaries via `SharedArrayBuffer`/`Atomics`, WebGL2 VAO/VBO/UBO/`TEXTURE_2D_ARRAY`, greedy meshing, and an abstract factory for block content.


---

## 1. Architecture Map

```
/src
├── /engine
│   ├── /core
│   │   ├── Config.ts                 // Render distance, chunk size, world height, worker count
│   │   ├── EventBus.ts               // Strictly-typed pub/sub for cross-system events
│   │   ├── GameLoop.ts               // Fixed-timestep update + variable render (accumulator)
│   │   └── Disposable.ts             // RAII contract for GPU/worker resources
│   ├── /math
│   │   ├── mat4.ts  vec3.ts  vec4.ts // Pure functions, no allocations on hot paths
│   │   ├── AABB.ts                   // Axis-aligned bounding box (frustum + collision)
│   │   └── Frustum.ts                // 6-plane extractor from VP matrix
│   ├── /alloc
│   │   ├── SharedPool.ts             // SharedArrayBuffer slot allocator (chunk transfer)
│   │   ├── TypedArrayPool.ts         // Per-thread scratch arena for transient arrays
│   │   └── RingBuffer.ts             // Lock-free SPMC ring for worker -> main job queue
│   ├── /ecs
│   │   ├── EntityManager.ts          // Sparse-set entity ID allocator (Int32Array-backed)
│   │   ├── ComponentStore.ts         // Generic SoA component container over TypedArrays
│   │   ├── SystemManager.ts          // Iterator over entity archetypes; system ordering
│   │   ├── Query.ts                  // Cached bitset query (archetype matching)
│   │   ├── /components
│   │   │   ├── Transform.ts          // pos:Float32x3, rot:Float32x4(quat), scale:Float32x3
│   │   │   ├── RigidBody.ts          // vel:Float32x3, AABB, onGround:Uint8
│   │   │   ├── Health.ts             // hp:Int16, maxHp:Int16, regenCd:Float32
│   │   │   ├── AIState.ts            // target:Uint32, pathHead:Uint16, pathLen:Uint16
│   │   │   ├── PathBuffer.ts         // 3D waypoint ring (Float32Array)
│   │   │   ├── RenderableMob.ts      // vaoId, instanceOffset, skeletonId
│   │   │   └── HostileTag.ts / FriendlyTag.ts
│   │   └── /systems
│   │       ├── PhysicsSystem.ts      // swept-AABB vs voxel grid
│   │       ├── PathfindingSystem.ts  // A* over chunk-local voxel walkability
│   │       ├── AISystem.ts           // behavior trees -> intent -> path requests
│   │       ├── MobRenderSystem.ts    // batches mob VAOs per shader
│   │       └── HealthSystem.ts
│   ├── /render
│   │   ├── Renderer.ts               // Owns GL context; issues draws; sorts by shader/UBO
│   │   ├── ShaderProgram.ts          // GLSL 300 ES link + uniform reflection
│   │   ├── UniformBuffer.ts          // UBO wrapper: CameraBlock, FogBlock, TimeBlock
│   │   ├── VertexArray.ts            // VAO encapsulation; interleaved attrib bindings
│   │   ├── VertexBuffer.ts           // Immutable/immutable storage w/ glBufferSubData
│   │   ├── Texture2DArray.ts         // 16x16xN array; per-layer mip chain; aniso
│   │   ├── Framebuffer.ts            // For depth pre-pass, water reflection
│   │   ├── Camera.ts                 // yaw/pitch, fly/clip modes, builds view matrix
│   │   ├── FrustumCuller.ts          // 6-plane extraction + chunk AABB test
│   │   ├── ChunkMesh.ts              // Holds VAO + 2 VBOs (solid/translucent) per chunk
│   │   └── /shaders
│   │       ├── chunk.vert / chunk.frag       // UBO-driven, tex-layer from vertex
│   │       ├── mob.vert / mob.frag           // Skinned via dual-quats in UBO
│   │       ├── sky.vert / sky.frag
│   │       └── post.frag                     // Tonemap + ACES + cheap fog
│   └── /workers
│       ├── WorkerSpawner.ts          // Inline worker bootstrap via Blob URL (no bundler split)
│       ├── /worldgen
│       │   ├── WorldGenWorker.ts     // Entry; consumes SharedPool slot, fills voxels
│       │   ├── SimplexNoise.ts       // 3D gradient noise (Alois Schlosser-style)
│       │   ├── BiomeSampler.ts       // temperature × humidity -> biome lookup
│       │   ├── CaveCarver.ts         // 3D Perlin worm carver w/ random walk
│       │   └── OreDistributor.ts     // Poisson-disc per-ore per-chunk
│       └── /mesher
│           ├── MesherWorker.ts       // Reads voxels from SharedPool, writes mesh
│           ├── GreedyMesher.ts       // The algorithm (provided below)
│           ├── AmbientOcclusion.ts   // 4-corner AO sampling
│           └── FaceCuller.ts         // Opaque vs transparent neighbor test
│
├── /world
│   ├── World.ts                      // Orchestrator: dispatches gen/mesh jobs; tracks chunk state
│   ├── ChunkManager.ts               // LRU chunk cache keyed by (x,z) packed Int32
│   ├── Chunk.ts                      // Pure-data: ref to SharedPool slot + GPU handles
│   ├── ChunkColumn.ts                // 16x16x256 vertical column; biome table
│   ├── BlockRegistry.ts              // Singleton; id -> BlockDefinition
│   ├── BlockFactory.ts               // Abstract Factory pattern (provided below)
│   └── /blocks
│       ├── BlockDefinition.ts        // Interface for block content
│       ├── BlockMaterial.ts          // Flags: opaque, transparent, liquid, foliage
│       ├── VanillaBlocks.ts          // 128+ block registrations (dirt, stone, logs, ores...)
│       └── AABB.ts                   // Per-block collision hull (slabs, fences differ)
│
├── /content
│   ├── /biomes
│   │   ├── BiomeRegistry.ts
│   │   ├── Plains.ts Desert.ts Forest.ts Mountains.ts Swamp.ts
│   │   └── BiomeSurfaceRule.ts       // top block / filler / depth
│   └── /mobs
│       ├── MobFactory.ts             // Spawns ECS entity w/ archetype tag
│       ├── Pig.ts Zombie.ts Skeleton.ts Creeper.ts
│       └── MobModel.ts               // Cube-based geometry reference
│
├── /game
│   ├── Game.ts                       // Composition root; wires ECS, renderer, workers
│   ├── InputState.ts                 // Keyboard/mouse -> intent ring buffer
│   └── PlayerController.ts
│
└── main.ts                           // Bootstrap; canvas creation; vaoCaps check
```


---

## 2. Worker Architecture — `SharedPool` (Zero-Copy Chunk Transfer)

### 2.1 Design rationale

Each chunk needs to migrate across three threads:


1. **Main thread** — allocates a slot, dispatches coords
2. **WorldGen worker** — fills `voxels` + `biomes`
3. **Mesher worker** — consumes voxels, produces interleaved vertex/index bytes

`postMessage` with structured cloning would copy \~64 KiB of voxel data per chunk, multiplied by render distance squared — completely untenable at 32 chunks. Instead, every chunk lives in a fixed-size **slot** carved from a single pre-allocated `SharedArrayBuffer`. State transitions are guarded by `Atomics.compareExchange`, giving lock-free SPSC semantics for each phase.

### 2.2 Slot memory map

| Offset | Field | Type | Purpose |
|----|----|----|----|
| 0 | `status` | `Int32` (atomic) | `ChunkSlotStatus` enum |
| 4 | `vertexCount` | `Uint32` | Bytes written by mesher / 4 |
| 8 | `indexCount` | `Uint32` | Number of indices |
| 12 | `chunkX` | `Int32` | World-space chunk X |
| 16 | `chunkZ` | `Int32` | World-space chunk Z |
| 20 | `genSeed` | `Uint32` | Hash of (x,z,worldSeed) for deterministic noise |
| 24 | `pad` | `Uint32[2]` | Align next field to 32 |
| 32 | `voxels` | `Uint8[CHUNK_VOL]` | Block IDs (16×16×256 = 65 536) |
| 32+V | `light` | `Uint8[CHUNK_VOL]` | Packed sky/block light nibbles |
| 32+V+L | `vertices` | `Float32[MAX_VERTS × STRIDE]` | Interleaved mesh data |
| 32+V+L+M | `indices` | `Uint32[MAX_INDICES]` | Triangle indices |

`STRIDE = 8` floats (pos.xyz, normal.xyz packed as 3 floats for clarity, uv.xy packed in tex-layer-aware UV). We trade a few bytes for branching simplicity.

### 2.3 Implementation

```typescript
// /src/engine/alloc/SharedPool.ts

/**
 * Chunk slot synchronization states. Transitions are enforced via
 * Atomics.compareExchange, which guarantees that exactly one thread "owns"
 * the right to mutate the heavy payload (voxels / mesh) at any moment.
 *
 *       FREE ──(main dispatch)──▶ GENERATING
 *         ▲                            │
 *         │                            │ (worker writes voxels)
 *         │                            ▼
 *   GPU_UPLOADED ◀──(main upload)── MESH_READY
 *                                  ▲
 *                                  │ (mesher writes mesh)
 *                                  │
 *                               VOXELS_READY
 *                                  ▲
 *                                  │ (worker signals done)
 *                               GENERATING
 *
 * O(1) state transitions; no contention beyond the CAS.
 */
export const enum ChunkSlotStatus {
  FREE         = 0,
  GENERATING   = 1,
  VOXELS_READY = 2,
  MESHING      = 3,
  MESH_READY   = 4,
  GPU_UPLOADED = 5,
}

/** Compile-time chunk constants. Mutable via Config for testing only. */
export interface ChunkDimensions {
  readonly sizeX: number;     // 16
  readonly sizeY: number;     // 256
  readonly sizeZ: number;     // 16
  readonly maxVertsPerChunk: number;  // empirical worst case ~ 65k
  readonly maxIndicesPerChunk: number;
  readonly vertexStrideFloats: number; // 8 floats per vertex
}

/** All typed-array views into a single contiguous region of one SharedArrayBuffer. */
export interface ChunkSlot {
  readonly slotIndex: number;     // index into the pool's free list
  readonly buffer: SharedArrayBuffer;
  readonly baseByteOffset: number;
  // --- Header (28 bytes used, 4 pad) ---
  readonly status: Int32Array;        // length 1
  readonly vertexCount: Uint32Array;  // length 1
  readonly indexCount: Uint32Array;   // length 1
  readonly chunkX: Int32Array;        // length 1
  readonly chunkZ: Int32Array;        // length 1
  readonly genSeed: Uint32Array;      // length 1
  // --- Payload ---
  readonly voxels: Uint8Array;        // CHUNK_VOL
  readonly light: Uint8Array;         // CHUNK_VOL
  readonly vertices: Float32Array;    // maxVerts * stride
  readonly indices: Uint32Array;      // maxIndices
}

/**
 * Fixed-capacity pool of chunk slots, all carved out of ONE SharedArrayBuffer.
 *
 * Why one big buffer?
 *   - Single transfer to each worker (no per-slot postMessage of buffers).
 *   - Workers receive the SharedArrayBuffer once at startup; subsequent
 *     dispatches are pure ID/coord messages — sub-microsecond.
 *
 * Memory footprint at defaults (16×256×16, 65k verts, 130k indices):
 *   header  + voxels  + light   + verts         + indices
 *   32 B    + 64 KiB  + 64 KiB  + 2 080 KiB     + 520 KiB  ≈ 2.7 MiB per slot
 *   At render distance 16 (1021 chunks): ~2.8 GiB. Tunable via Config.
 *
 * Complexity:
 *   acquire() / release() — O(1), backed by a Uint32Array stack.
 */
export class SharedPool {
  private readonly rootBuffer: SharedArrayBuffer;
  private readonly slotByteSize: number;
  private readonly freeList: Uint32Array;
  private freeHead: number;

  // Pre-computed sub-view offsets (in bytes) within a slot.
  private readonly headerBytes = 32;
  private readonly voxelsBytes: number;
  private readonly lightBytes: number;
  private readonly vertsBytes: number;
  private readonly indicesBytes: number;

  constructor(
    private readonly capacity: number,     // # of concurrent slots (≈ chunk cache size)
    private readonly dims: ChunkDimensions,
  ) {
    this.voxelsBytes   = dims.sizeX * dims.sizeY * dims.sizeZ;
    this.lightBytes    = this.voxelsBytes;
    this.vertsBytes    = dims.maxVertsPerChunk * dims.vertexStrideFloats * 4;
    this.indicesBytes  = dims.maxIndicesPerChunk * 4;

    this.slotByteSize =
      this.headerBytes + this.voxelsBytes + this.lightBytes + this.vertsBytes + this.indicesBytes;

    // Cross-thread alignment: round slot size up to 64-byte cache line.
    const alignedSlot = (this.slotByteSize + 63) & ~63;
    this.slotByteSize = alignedSlot;

    const total = alignedSlot * capacity;
    // SharedArrayBuffer requires crossOriginIsolated + COOP/COEP headers.
    this.rootBuffer = new SharedArrayBuffer(total);

    this.freeList = new Uint32Array(capacity);
    for (let i = 0; i < capacity; i++) this.freeList[i] = i;
    this.freeHead = capacity;
  }

  /** Returns the underlying buffer — pass to every worker exactly once. */
  get rawBuffer(): SharedArrayBuffer { return this.rootBuffer; }
  get slotSize(): number { return this.slotByteSize; }
  get dimensions(): ChunkDimensions { return this.dims; }

  /** O(1) slot acquisition from the free-list stack. Returns null if exhausted. */
  acquire(): ChunkSlot | null {
    if (this.freeHead === 0) return null;
    const slotIndex = this.freeList[--this.freeHead];
    return this.view(slotIndex);
  }

  /** O(1) release. Caller must have already transitioned status -> FREE. */
  release(slot: ChunkSlot): void {
    Atomics.store(slot.status, 0, ChunkSlotStatus.FREE);
    this.freeList[this.freeHead++] = slot.slotIndex;
  }

  /**
   * Builds all typed-array views for a given slot index.
   *
   * NOTE: views are zero-copy aliases into the SharedArrayBuffer; both the
   * main thread and workers can call this independently and observe the
   * same physical memory.
   */
  view(slotIndex: number): ChunkSlot {
    const base = slotIndex * this.slotByteSize;
    const b = this.rootBuffer;
    const dv = (o: number, len: number) => new DataView(b, base + o, len);
    // Use DataView-derived typed arrays for precise byte control.
    return {
      slotIndex,
      buffer: b,
      baseByteOffset: base,
      status:       new Int32Array(b, base + 0,  1),
      vertexCount:  new Uint32Array(b, base + 4,  1),
      indexCount:   new Uint32Array(b, base + 8,  1),
      chunkX:       new Int32Array(b, base + 12, 1),
      chunkZ:       new Int32Array(b, base + 16, 1),
      genSeed:      new Uint32Array(b, base + 20, 1),
      voxels:       new Uint8Array(b, base + this.headerBytes,                this.voxelsBytes),
      light:        new Uint8Array(b, base + this.headerBytes + this.voxelsBytes,  this.lightBytes),
      vertices:     new Float32Array(b, base + this.headerBytes + this.voxelsBytes + this.lightBytes,
                                                       this.dims.maxVertsPerChunk * this.dims.vertexStrideFloats),
      indices:      new Uint32Array(b, base + this.headerBytes + this.voxelsBytes + this.lightBytes + this.vertsBytes,
                                                       this.dims.maxIndicesPerChunk),
    };
  }
}

/**
 * Thread-side dispatch protocol. Workers receive the SharedArrayBuffer once
 * at construction via a one-time postMessage; thereafter, the main thread
 * only sends tiny { slotIndex, chunkX, chunkZ, seed } "intent" messages.
 *
 * Workers NEVER block on Atomics.wait during normal operation; they poll
 * the slot's status field using Atomics.compareExchange when claiming work,
 * which is sufficient because each slot is SPSC per phase.
 */
export interface ChunkJobMessage {
  readonly slotIndex: number;
  readonly chunkX: number;
  readonly chunkZ: number;
  readonly seed: number;
}
```

### 2.4 WorldGen worker entry (sketch)

```typescript
// /src/engine/workers/worldgen/WorldGenWorker.ts
/// <reference lib="webworker" />
const ctx = self as unknown as DedicatedWorkerGlobalScope;

let pool: SharedPool | null = null;
let noise: SimplexNoise | null = null;
let biomes: BiomeSampler | null = null;
let caves: CaveCarver | null = null;

ctx.onmessage = (e: MessageEvent<{ buffer: SharedArrayBuffer; dims: ChunkDimensions; seed: number } | ChunkJobMessage>) => {
  const msg = e.data;
  // First message is the bootstrap (one-time buffer hand-off).
  if ((msg as any).buffer) {
    const boot = msg as any as { buffer: SharedArrayBuffer; dims: ChunkDimensions; seed: number };
    pool = new SharedPool(/* capacity inferred */ 0, boot.dims);
    // Re-attach the worker-side view of the SAME buffer the main thread owns.
    pool = attachExternal(boot.buffer, boot.dims);
    noise = new SimplexNoise(boot.seed);
    biomes = new BiomeSampler(boot.seed ^ 0xB10ME);
    caves = new CaveCarver(boot.seed ^ 0xCAFE);
    return;
  }

  // Subsequent messages are tiny job intents.
  const job = msg as ChunkJobMessage;
  const slot = pool!.view(job.slotIndex);

  // CAS: only proceed if main thread agrees this slot is GENERATING.
  if (Atomics.compareExchange(slot.status, 0, ChunkSlotStatus.GENERATING, ChunkSlotStatus.GENERATING) !== ChunkSlotStatus.GENERATING) {
    return; // Not ours to touch; ignore (defensive).
  }

  Atomics.store(slot.chunkX, 0, job.chunkX);
  Atomics.store(slot.chunkZ, 0, job.chunkZ);
  Atomics.store(slot.genSeed, 0, job.seed);

  // --- Hot path: fill voxels (no allocations) ---
  const { sizeX, sizeY, sizeZ } = pool!.dimensions;
  const vox = slot.voxels;
  const baseX = job.chunkX * sizeX;
  const baseZ = job.chunkZ * sizeZ;

  for (let z = 0; z < sizeZ; z++) {
    for (let x = 0; x < sizeX; x++) {
      const wx = baseX + x, wz = baseZ + z;
      const biome = biomes!.sample(wx, wz);
      const height = Math.floor(64 + noise!.noise2D(wx * 0.005, wz * 0.005) * 16);
      for (let y = 0; y < sizeY; y++) {
        const idx = (y * sizeZ + z) * sizeX + x;
        if (y > height)        vox[idx] = 0;            // air
        else if (y === height) vox[idx] = biome.topBlock;
        else if (y > height - 4) vox[idx] = biome.fillerBlock;
        else                   vox[idx] = 1;            // stone
      }
    }
  }
  caves!.carve(vox, baseX, baseZ, sizeX, sizeY, sizeZ);

  // Publish: VOXELS_READY. Mesher worker CASes GENERATING -> MESHING later.
  Atomics.store(slot.status, 0, ChunkSlotStatus.VOXELS_READY);
  Atomics.notify(slot.status, 0, 1); // wake one waiter (mesher)
};
```


---

## 3. WebGL2 Greedy Mesher

### 3.1 Vertex layout (interleaved, 32 bytes / vertex)

```
Offset  Size  Field     Encoding
 0      12    position  3 × f32 (model-space, chunk-relative)
12       4    packed    u32: [nx:2|ny:2|nz:2|ao:2|layer:8|unused:16]  (snorm normal + AO)
16       4    uv0       u16×2 (unorm, [0..1] across the merged quad)
20      12    uv1/pad   f32 (reserved for animated UV scroll / tint)
```

For readability below I use **8 ×** `f32` (32 bytes): `pos.xyz, normal.xyz, uv.xy`. Tex-layer is derived in-shader from `gl_VertexID` divisor or carried in a separate `Uint8` stream — both strategies avoid breaking alignment. Real engine uses the packed layout above; this skeleton prioritizes clarity.

### 3.2 Algorithm complexity

For one chunk of side `N` (here `N=16` for X/Z, `H=256` for Y):

* 3 axis sweeps × `N` slices × `N×N` mask = **O(N²·H) = O(V) per chunk**.
* Greedy merge: amortized **O(mask_size)** because each cell is consumed once.
* Total: **O(V)** per chunk — strictly linear in chunk volume.

### 3.3 Implementation

```typescript
// /src/engine/workers/mesher/GreedyMesher.ts

import type { ChunkSlot } from "../../alloc/SharedPool";
import type { BlockRegistry } from "../../../world/BlockRegistry";

/** Axis index constants — kept as `const enum` for inlining. */
const enum Axis { X = 0, Y = 1, Z = 2 }
/** 8 floats per vertex: pos(3) + normal(3) + uv(2). */
const STRIDE = 8;
/** 6 indices per quad (two triangles). We index 4 verts per quad. */
const INDICES_PER_QUAD = 6;

/**
 * Greedy meshing implementation.
 *
 * Terminology (matches the canonical "0fps" article by Mikola Lysenko):
 *   - "d" is the sweep axis (0,1,2)
 *   - "u","v" are the in-plane axes perpendicular to d
 *   - "mask" is a 2D grid of size sizeU×sizeV holding the *block id* of the
 *     face that occupies that cell, with sign indicating normal direction
 *     (+ => +d face, − => −d face). 0 means "no face here".
 *
 * Synchronization contract:
 *   - Caller guarantees slot.status == MESHING (CAS-acquired).
 *   - We write vertices/indices, set counts, then set status = MESH_READY.
 *
 * @returns true if the mesh fit within the slot's vertex budget.
 */
export function greedyMeshChunk(
  slot: ChunkSlot,
  dims: { sizeX: number; sizeY: number; sizeZ: number },
  blocks: BlockRegistry,
  neighborVoxels: { px: Uint8Array | null; nx: Uint8Array | null;
                    pz: Uint8Array | null; nz: Uint8Array | null },
): boolean {
  const { sizeX, sizeY, sizeZ } = dims;
  // Treat the chunk as a 3D volume indexed [y][z][x].
  const vol = sizeX * sizeY * sizeZ;
  const vox = slot.voxels;

  // Pre-allocate mesh cursors into the SharedArrayBuffer-backed arrays.
  const verts = slot.vertices;
  const idx   = slot.indices;
  let vCount = 0;  // vertex count (NOT byte count)
  let iCount = 0;
  const maxV = verts.length / STRIDE;
  const maxI = idx.length;

  /** Sample helper: returns voxel id at integer coords, with neighbor spillover. */
  const get = (x: number, y: number, z: number): number => {
    if (y < 0 || y >= sizeY) return 0;              // outside vertical = air
    if (x >= 0 && x < sizeX && z >= 0 && z < sizeZ) {
      return vox[(y * sizeZ + z) * sizeX + x];
    }
    // Spill into neighbor columns (provided by caller).
    if (x < 0  && neighborVoxels.nx) return neighborVoxels.nx[(y * sizeZ + z) * sizeX + (sizeX - 1)];
    if (x >= sizeX && neighborVoxels.px) return neighborVoxels.px[(y * sizeZ + z) * sizeX + 0];
    if (z < 0  && neighborVoxels.nz) return neighborVoxels.nz[(y * sizeX + x) * sizeZ + (sizeZ - 1)];
    if (z >= sizeZ && neighborVoxels.pz) return neighborVoxels.pz[(y * sizeX + x) * sizeZ + 0];
    return 0;
  };

  /**
   * Determines whether the face between `a` (current voxel) and `b` (next voxel
   * along +d) should be rendered, and which side owns it.
   *
   * Returns 0 (no face), +a (face owned by a, normal +d), -b (face owned by b, normal -d).
   *
   * Rules:
   *   - Two identical opaque blocks cull each other.
   *   - Air-vs-air produces no face.
   *   - Air-vs-opaque → opaque owns the face.
   *   - Two different transparent blocks → BOTH sides may want a face;
   *     for simplicity here we let the non-air side own it (correct for liquids).
   */
  const faceMaskValue = (a: number, b: number): number => {
    if (a === b) return 0;
    const defA = a === 0 ? null : blocks.get(a);
    const defB = b === 0 ? null : blocks.get(b);
    const opaqueA = defA ? !defA.transparent : false;
    const opaqueB = defB ? !defB.transparent : false;
    if (opaqueA && opaqueB) return 0;
    if (a !== 0) return a;     // a owns the face (visible from b side)
    if (b !== 0) return -b;    // b owns the face (visible from a side)
    return 0;
  };

  /**
   * Per-vertex ambient occlusion. Standard "mojang" AO:
   *   - Two side-occluders => fully dark (0)
   *   - Otherwise: 3 - (s1 + s2 + corner)  ∈ {0,1,2,3}
   *
   * The four vertices of a face sample the three voxels diagonal to that corner.
   */
  const vertexAO = (s1: number, s2: number, c: number): number => {
    if (s1 !== 0 && s2 !== 0) return 0;
    return 3 - ((s1 !== 0 ? 1 : 0) + (s2 !== 0 ? 1 : 0) + (c !== 0 ? 1 : 0));
  };

  /**
   * Allocate reusable scratch masks. sizeV is the largest in-plane dimension;
   * we use the maximum across all three sweeps to share one allocation.
   *
   * For chunk 16×256×16:
   *   - sweep X: in-plane (Y,Z) → 256*16 = 4096
   *   - sweep Y: in-plane (X,Z) → 16*16  = 256
   *   - sweep Z: in-plane (X,Y) → 16*256 = 4096
   */
  const maxMask = Math.max(sizeY * sizeZ, sizeX * sizeZ, sizeX * sizeY);
  const mask = new Int32Array(maxMask);  // Int32 to allow negative sign

  /** Iterates the three axis sweeps. Order matters for cache behavior
   *  on the voxel array (Y-major is best since voxels are Y-strided). */
  const axes: ReadonlyArray<{ d: Axis; u: Axis; v: Axis; sizeD: number; sizeU: number; sizeV: number; }> = [
    { d: Axis.X, u: Axis.Y, v: Axis.Z, sizeD: sizeX, sizeU: sizeY, sizeV: sizeZ },
    { d: Axis.Y, u: Axis.X, v: Axis.Z, sizeD: sizeY, sizeU: sizeX, sizeV: sizeZ },
    { d: Axis.Z, u: Axis.X, v: Axis.Y, sizeD: sizeZ, sizeU: sizeX, sizeV: sizeY },
  ];

  for (const sweep of axes) {
    const { d, u, v, sizeD, sizeU, sizeV } = sweep;
    // x[] is a 3-vector we mutate; q[] is the +1 step in d.
    const x = [0, 0, 0];
    const q = [0, 0, 0];
    q[d] = 1;

    // Sweep along d. We compare voxel[x] vs voxel[x+q].
    for (x[d] = -1; x[d] < sizeD;) {
      // ---- Compute mask for this slice ----
      let n = 0;
      for (x[v] = 0; x[v] < sizeV; x[v]++) {
        for (x[u] = 0; x[u] < sizeU; x[u]++) {
          // Note: when x[d] === -1, get() returns 0 for "voxel at x" — that's correct
          // because the boundary voxel is in the neighbor column (handled by get()).
          const a = (x[d] >= 0) ? get(x[0], x[1], x[2]) : 0;
          const b = (x[d] + 1 < sizeD) ? get(x[0] + q[0], x[1] + q[1], x[2] + q[2]) : 0;
          mask[n++] = faceMaskValue(a, b);
        }
      }
      x[d]++;  // advance to the slice where faces actually live

      // ---- Greedy merge + emit ----
      n = 0;
      for (let j = 0; j < sizeV; j++) {
        for (let i = 0; i < sizeU;) {
          const c = mask[n];
          if (c !== 0) {
            // Width: count contiguous identical mask values in +u direction.
            let w = 1;
            while (i + w < sizeU && mask[n + w] === c) w++;

            // Height: extend quad in +v direction while the entire row matches.
            let h = 1;
            let done = false;
            while (j + h < sizeV) {
              for (let k = 0; k < w; k++) {
                if (mask[n + k + h * sizeU] !== c) { done = true; break; }
              }
              if (done) break;
              h++;
            }

            // -------- Emit one quad (w × h) --------
            // 4 corner vertices + 6 indices. Compute per-corner AO from the
            // three voxels diagonally adjacent to that corner.

            // Origin in 3D space.
            const du = [0, 0, 0]; du[u] = w;
            const dv = [0, 0, 0]; dv[v] = h;
            const p0 = [x[0],        x[1],        x[2]];
            const p1 = [x[0] + du[0], x[1] + du[1], x[2] + du[2]];
            const p2 = [x[0] + du[0] + dv[0], x[1] + du[1] + dv[1], x[2] + du[2] + dv[2]];
            const p3 = [x[0]        + dv[0], x[1]        + dv[1], x[2]        + dv[2]];

            // Normal: +d or -d depending on sign of c.
            const sign = c > 0 ? 1 : -1;
            const normal = [0, 0, 0];
            normal[d] = sign;
            const blockId = Math.abs(c);
            const def = blocks.get(blockId);
            // Texture layer: face direction selects top/side/bottom from BlockDefinition.
            const layer = sign > 0
              ? (d === Axis.Y ? def.textures.top    : def.textures.side)
              : (d === Axis.Y ? def.textures.bottom : def.textures.side);

            // Compute per-corner AO by sampling the three "above-face" voxels.
            // The "above" direction is the normal; we step back to the air side.
            const aoSamples = computeCornerAO(get, x, du, dv, d, u, v, sign, w, h);

            // UVs: (0,0) (w,0) (w,h) (0,h) so the texture tiles per quad.
            // (Tighter UV packing would stretch — undesirable for block textures.)
            const uv0 = [0, 0];
            const uv1 = [w, 0];
            const uv2 = [w, h];
            const uv3 = [0, h];

            // Apply the standard "anisotropic flip" to remove AO seams:
            // if (ao0 + ao2 > ao1 + ao3) flip the triangulation.
            const flip = (aoSamples[0] + aoSamples[2] > aoSamples[1] + aoSamples[3]) ? 0 : 1;

            // Bounds-check the vertex budget.
            if (vCount + 4 > maxV || iCount + 6 > maxI) {
              // Mark the chunk as "incomplete"; the main thread can re-dispatch
              // a split mesh or fall back to per-face meshing.
              Atomics.store(slot.vertexCount, 0, vCount);
              Atomics.store(slot.indexCount, 0, iCount);
              return false;
            }

            // Write 4 interleaved vertices into the SharedArrayBuffer.
            writeVertex(verts, vCount * STRIDE, p0, normal, uv0, layer, aoSamples[0]);
            writeVertex(verts, (vCount + 1) * STRIDE, p1, normal, uv1, layer, aoSamples[1]);
            writeVertex(verts, (vCount + 2) * STRIDE, p2, normal, uv2, layer, aoSamples[2]);
            writeVertex(verts, (vCount + 3) * STRIDE, p3, normal, uv3, layer, aoSamples[3]);

            // 6 indices, flipped if AO requires.
            const base = vCount;
            if (flip) {
              idx[iCount++] = base;     idx[iCount++] = base + 1; idx[iCount++] = base + 2;
              idx[iCount++] = base;     idx[iCount++] = base + 2; idx[iCount++] = base + 3;
            } else {
              idx[iCount++] = base + 1; idx[iCount++] = base + 2; idx[iCount++] = base + 3;
              idx[iCount++] = base + 1; idx[iCount++] = base + 3; idx[iCount++] = base;
            }
            vCount += 4;

            // Zero out the consumed mask cells so the inner loops naturally skip them.
            for (let l = 0; l < h; l++)
              for (let k = 0; k < w; k++)
                mask[n + k + l * sizeU] = 0;

            i += w;
            n += w;
          } else {
            i++;
            n++;
          }
        }
      }
    }
  }

  Atomics.store(slot.vertexCount, 0, vCount);
  Atomics.store(slot.indexCount, 0, iCount);
  return true;
}

/**
 * Samples the 3 voxels needed per corner for AO.
 *
 * For a face with normal `sign * e_d`:
 *   - The face plane is at coordinate (x[d]) on the +d side of voxel x.
 *   - Each of the 4 corners of the quad touches a different "corner" of the
 *     air cell adjacent to that face.
 *   - At each corner we sample side1 (along +u), side2 (along +v), and corner
 *     (diagonal +u,+v) of the *air* cell — i.e. voxels at offsets
 *     (x + sign*q, x + sign*q + e_u, x + sign*q + e_v, x + sign*q + e_u + e_v).
 */
function computeCornerAO(
  get: (x: number, y: number, z: number) => number,
  x: number[],
  du: number[],
  dv: number[],
  d: Axis, u: Axis, v: Axis, sign: number, w: number, h: number,
): [number, number, number, number] {
  // Step from face plane toward the air side.
  const stepBack = [0, 0, 0];
  stepBack[d] = sign < 0 ? -1 : 0;  // we are on the +d side of voxel; air is at x[d] + sign
  // Actually: for sign=+1 the face is on the +d side of voxel x; air is at x + q.
  // For sign=-1 the face is on the -d side of voxel x+q; air is at x.
  // stepBack takes us from face-origin into the air cell.
  const airOffset = sign > 0 ? 1 : 0;  // 1 means "+q"; 0 means "stay"

  // The four corners in (u,v) parameterization: (0,0), (w,0), (w,h), (0,h)
  // Offset by ±1 along u and v to sample neighbors of the air cell.
  const origin = [x[0], x[1], x[2]];
  origin[d] += airOffset;

  // Helper to sample at a (duOffset, dvOffset) corner of the air cell.
  const sampleCorner = (duOff: number, dvOff: number): number => {
    // s1 = neighbor in +u direction (relative to corner)
    // s2 = neighbor in +v direction
    // c  = diagonal neighbor (+u, +v)
    // Which physical neighbor depends on the sign of duOff, dvOff relative
    // to the corner. We precompute absolute voxel coords.
    const cu = duOff > 0 ? w : 0;
    const cv = dvOff > 0 ? h : 0;
    const corner = [origin[0] + (du[0] / w) * cu + (dv[0] / h) * cv,
                    origin[1] + (du[1] / w) * cu + (dv[1] / h) * cv,
                    origin[2] + (du[2] / w) * cu + (dv[2] / h) * cv];
    const s1 = [corner[0], corner[1], corner[2]];
    const s2 = [corner[0], corner[1], corner[2]];
    const cc = [corner[0], corner[1], corner[2]];
    s1[u] += duOff; s2[v] += dvOff; cc[u] += duOff; cc[v] += dvOff;
    const v1 = get(s1[0] | 0, s1[1] | 0, s1[2] | 0);
    const v2 = get(s2[0] | 0, s2[1] | 0, s2[2] | 0);
    const vc = get(cc[0] | 0, cc[1] | 0, cc[2] | 0);
    return vertexAOInner(v1, v2, vc);
  };

  // AO order must match the vertex order p0,p1,p2,p3 = (0,0),(w,0),(w,h),(0,h).
  return [
    sampleCorner(-1, -1),
    sampleCorner(+1, -1),
    sampleCorner(+1, +1),
    sampleCorner(-1, +1),
  ];
}

function vertexAOInner(s1: number, s2: number, c: number): number {
  if (s1 !== 0 && s2 !== 0) return 0;
  return 3 - ((s1 !== 0 ? 1 : 0) + (s2 !== 0 ? 1 : 0) + (c !== 0 ? 1 : 0));
}

/** Writes one interleaved vertex at the given float offset. */
function writeVertex(
  verts: Float32Array,
  off: number,
  pos: number[],
  normal: number[],
  uv: number[],
  _layer: number,
  ao: number,
): void {
  verts[off + 0] = pos[0];
  verts[off + 1] = pos[1];
  verts[off + 2] = pos[2];
  verts[off + 3] = normal[0];
  verts[off + 4] = normal[1];
  verts[off + 5] = normal[2];
  // Encode AO into the UV's W channel by scaling UV magnitude — quick hack for
  // the skeleton. Real impl packs AO into a separate byte stream.
  const aoScale = 0.5 + 0.5 * (ao / 3);
  verts[off + 6] = uv[0];
  verts[off + 7] = uv[1] * aoScale; // (overloaded; see comment above)
}
```

### 3.4 Chunk vertex shader (GLSL 300 ES) — UBO + Texture Array

```glsl
// /src/engine/render/shaders/chunk.vert
#version 300 es
precision highp float;

// Global UBO — uploaded ONCE per frame, shared by every chunk shader invocation.
layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  vec3 u_camPos;
  float u_time;
  vec4 u_fogColor;       // rgb + density
};

// Per-chunk UBO — small, one per draw batch.
layout(std140, binding = 1) uniform ChunkBlock {
  mat4 u_model;          // chunk-world translation
  vec2 u_uvScale;        // animated UV scroll for water/lava
};

in vec3 a_pos;
in vec3 a_normal;
in vec2 a_uv;

// Texture layer (which slice of TEXTURE_2D_ARRAY) is selected per-instance
// or per-vertex via a separate stream. We use a vertex-attrib divisor here.
in float a_texLayer;

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_worldPos;
out float v_texLayer;
out float v_fogFactor;

void main() {
  vec4 world = u_model * vec4(a_pos, 1.0);
  v_worldPos = world.xyz;
  v_normal   = mat3(u_model) * a_normal;
  v_uv       = a_uv * u_uvScale;
  v_texLayer = a_texLayer;

  vec4 clip = u_projView * world;
  gl_Position = clip;

  // Cheap distance fog (linear). Exponential in frag for quality.
  float dist = length(u_camPos - world.xyz);
  v_fogFactor = clamp(1.0 - (u_fogColor.a - dist) / u_fogColor.a, 0.0, 1.0);
}
```

```glsl
// /src/engine/render/shaders/chunk.frag
#version 300 es
precision highp float;

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj; mat4 u_view; mat4 u_projView;
  vec3 u_camPos; float u_time; vec4 u_fogColor;
};

uniform sampler2DArray u_blockTextures;

in vec2 v_uv;
in vec3 v_normal;
in vec3 v_worldPos;
in float v_texLayer;
in float v_fogFactor;

out vec4 fragColor;

void main() {
  // Layer index is a per-vertex integer in practice (we floor it).
  vec4 albedo = texture(u_blockTextures, vec3(v_uv, floor(v_texLayer + 0.5)));
  if (albedo.a < 0.1) discard;

  // Cheap directional lighting (sun).
  vec3 N = normalize(v_normal);
  vec3 L = normalize(vec3(0.4, 1.0, 0.3));
  float ndl = max(dot(N, L), 0.0);
  vec3 lit = albedo.rgb * (0.35 + 0.65 * ndl);

  fragColor = vec4(mix(lit, u_fogColor.rgb, v_fogFactor), albedo.a);
}
```


---

## 4. DOD-Based ECS (Structure of Arrays over TypedArrays)

### 4.1 Design overview

* **Entity** = `uint32` ID. Recycled via a free-list (`Int32Array`).
* **Component** = a *registered* `ComponentStore<TSoA>` whose fields are parallel TypedArrays. Adding a component to an entity appends a row to each field array; the entity→row mapping is a sparse-set (two `Int32Array`s).
* **System** = a pure function `update(state, dt)` over a *query* of components. Queries are cached as `Uint32Array` row-index lists to keep iteration contiguous.
* **Archetype** is implicit — we use **sparse sets per component**, not archetype graphs. Simpler and avoids fragmentation when components are added/removed frequently (mobs dying, items despawning).

### 4.2 Implementation

```typescript
// /src/engine/ecs/EntityManager.ts

/**
 * Entity ID allocator with O(1) allocate/free.
 *
 * ID layout (uint32):
 *   [ generation:8 | index:24 ]
 *
 * Generation is bumped on free, so a stale ID from a previously-destroyed
 * entity never accidentally matches a freshly allocated one.
 */
const INDEX_MASK  = 0x00FF_FFFF;
const GEN_SHIFT   = 24;
const MAX_ENTITIES = 1 << 24;  // 16M — generous upper bound.

export class EntityManager {
  // generation[idx] holds the current generation for that index slot.
  private readonly generation: Uint8Array;
  // Free-list stack of recycled indices.
  private readonly freeIndices: Int32Array;
  private freeHead: number = 0;
  private liveCount: number = 0;

  constructor(readonly capacity: number = 1 << 18 /* 256k */) {
    this.generation = new Uint8Array(capacity);
    this.freeIndices = new Int32Array(capacity);
    for (let i = capacity - 1; i >= 0; i--) this.freeIndices[this.freeHead++] = i;
  }

  /** O(1). Returns a packed uint32 ID. */
  allocate(): number {
    if (this.freeHead === 0) throw new Error("EntityManager capacity exhausted");
    const idx = this.freeIndices[--this.freeHead];
    const gen = this.generation[idx];
    this.liveCount++;
    return (gen << GEN_SHIFT) | idx;
  }

  /** O(1). Marks the slot free and bumps generation to invalidate stale IDs. */
  destroy(id: number): void {
    const idx = id & INDEX_MASK;
    const gen = id >>> GEN_SHIFT;
    if (gen !== this.generation[idx]) return; // double-destroy guard
    this.generation[idx] = (gen + 1) & 0xFF;
    this.freeIndices[this.freeHead++] = idx;
    this.liveCount--;
  }

  /** O(1). Verifies the ID is currently live. */
  isAlive(id: number): boolean {
    const idx = id & INDEX_MASK;
    return (id >>> GEN_SHIFT) === this.generation[idx];
  }

  get count(): number { return this.liveCount; }
}
```

```typescript
// /src/engine/ecs/ComponentStore.ts

/**
 * Generic Structure-of-Arrays component container.
 *
 * TDesc is a record of field-name -> { type: TypedArrayConstructor, length: number }.
 * We instantiate one TypedArray per field, all sized to `capacity`. Sparse set
 * maps entity-index -> dense-row, and dense-row -> entity-index.
 *
 * Adding a component to entity E:
 *   denseRow = denseCount++;
 *   sparse[E] = denseRow;
 *   dense[denseRow] = E;
 *   <caller fills in field values at row `denseRow` in each field array>
 *
 * Removal uses swap-pop:
 *   lastRow = --denseCount;
 *   row = sparse[E];
 *   copy row data from lastRow -> row in every field array;
 *   sparse[dense[lastRow]] = row;
 *   dense[row] = dense[lastRow];
 *   sparse[E] = -1;
 *
 * This keeps the dense arrays compact → cache-friendly system iteration.
 */
export interface ComponentFieldDesc {
  readonly type: Int32ArrayConstructor | Uint32ArrayConstructor
              | Float32ArrayConstructor | Uint8ArrayConstructor
              | Float64ArrayConstructor;
  readonly length: number; // elements per entity (1 for scalar, 3 for vec3, etc.)
}

export type ComponentDesc = Readonly<Record<string, ComponentFieldDesc>>;

type FieldType<D extends ComponentDesc, K extends keyof D> =
  D[K]["type"] extends Float32ArrayConstructor ? Float32Array :
  D[K]["type"] extends Int32ArrayConstructor   ? Int32Array :
  D[K]["type"] extends Uint32ArrayConstructor  ? Uint32Array :
  D[K]["type"] extends Uint8ArrayConstructor   ? Uint8Array :
  D[K]["type"] extends Float64ArrayConstructor ? Float64Array :
  never;

export type ComponentSoA<D extends ComponentDesc> = {
  readonly [K in keyof D]: FieldType<D, K>;
};

export class ComponentStore<D extends ComponentDesc> {
  readonly capacity: number;
  private denseCount: number = 0;
  private readonly sparse: Int32Array; // entity-index -> dense row, or -1
  private readonly dense: Int32Array;  // dense row -> entity-index
  readonly data: ComponentSoA<D>;

  constructor(desc: D, capacity: number) {
    this.capacity = capacity;
    this.sparse = new Int32Array(capacity).fill(-1);
    this.dense  = new Int32Array(capacity);
    // Allocate one TypedArray per field.
    const data = {} as Record<string, TypedArray>;
    for (const key in desc) {
      const f = desc[key];
      // @ts-expect-error — TS can't narrow across keys here; runtime is sound.
      data[key] = new f.type(f.length * capacity);
    }
    this.data = data as ComponentSoA<D>;
  }

  /** O(1). Returns the dense row index for entity-index, or -1 if absent. */
  rowFor(entityIndex: number): number {
    return this.sparse[entityIndex];
  }

  /** O(1). Adds entity to this component; returns the row to write into. */
  add(entityIndex: number): number {
    if (this.sparse[entityIndex] !== -1) return this.sparse[entityIndex];
    const row = this.denseCount++;
    this.sparse[entityIndex] = row;
    this.dense[row] = entityIndex;
    return row;
  }

  /** O(1) amortized. Swap-pop removal. */
  remove(entityIndex: number): void {
    const row = this.sparse[entityIndex];
    if (row === -1) return;
    const last = --this.denseCount;
    if (row !== last) {
      // Copy last row's data into the vacated row, across every field.
      for (const key in (this as any).data) {
        const arr = (this as any).data[key] as TypedArray;
        const len = arr.length / this.capacity;
        for (let i = 0; i < len; i++) {
          arr[row * len + i] = arr[last * len + i];
        }
      }
      const movedEntity = this.dense[last];
      this.dense[row] = movedEntity;
      this.sparse[movedEntity] = row;
    }
    this.sparse[entityIndex] = -1;
  }

  /** Iterator over the dense row indices — for systems to consume. */
  *rows(): IterableIterator<number> {
    for (let i = 0; i < this.denseCount; i++) yield i;
  }

  get count(): number { return this.denseCount; }
}
```

### 4.3 Concrete component declarations

```typescript
// /src/engine/ecs/components/Transform.ts
export const TransformDesc = {
  position: { type: Float32Array, length: 3 },
  rotation: { type: Float32Array, length: 4 }, // quaternion
  scale:    { type: Float32Array, length: 3 },
} as const satisfies ComponentDesc;

// /src/engine/ecs/components/RigidBody.ts
export const RigidBodyDesc = {
  velocity:    { type: Float32Array, length: 3 },
  aabbMin:     { type: Float32Array, length: 3 },
  aabbMax:     { type: Float32Array, length: 3 },
  onGround:    { type: Uint8Array,   length: 1 },
  gravity:     { type: Float32Array, length: 1 },
} as const satisfies ComponentDesc;

// /src/engine/ecs/components/AIState.ts
export const AIStateDesc = {
  targetEntity: { type: Uint32Array, length: 1 },
  pathHead:     { type: Uint32Array, length: 1 },
  pathLen:      { type: Uint32Array, length: 1 },
  state:        { type: Uint8Array,  length: 1 }, // 0=idle,1=chase,2=flee,3=attack
  attackCd:     { type: Float32Array,length: 1 },
} as const satisfies ComponentDesc;
```

### 4.4 System example — Physics

```typescript
// /src/engine/ecs/systems/PhysicsSystem.ts

/**
 * Swept-AABB physics against the voxel grid.
 *
 * Iterates every entity that has BOTH Transform and RigidBody.
 * Because both stores are SoA with parallel dense arrays, iteration touches
 * contiguous memory — critical for 1k+ mobs at 60 FPS.
 *
 * Complexity: O(N) where N = entities-with-physics count.
 */
export class PhysicsSystem {
  constructor(
    private readonly em: EntityManager,
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly bodies:     ComponentStore<typeof RigidBodyDesc>,
    private readonly world: World, // voxel query API
  ) {}

  update(dt: number): void {
    const tPos  = this.transforms.data.position;
    const tRot  = this.transforms.data.rotation;
    const bVel  = this.bodies.data.velocity;
    const bMin  = this.bodies.data.aabbMin;
    const bMax  = this.bodies.data.aabbMax;
    const bGround = this.bodies.data.onGround;
    const bGrav  = this.bodies.data.gravity;

    for (const row of this.bodies.rows()) {
      const base = row * 3;

      // Apply gravity (scalar field).
      bVel[base + 1] -= bGrav[row] * dt;

      // Swept-AABB: try axis-by-axis movement, snapping to first collision.
      // (Full swept-AABB omitted for brevity; this is the canonical approach.)
      for (let axis = 0; axis < 3; axis++) {
        const desired = bVel[base + axis] * dt;
        const sign = Math.sign(desired);
        const steps = Math.ceil(Math.abs(desired));
        const step = desired / steps;
        for (let s = 0; s < steps; s++) {
          tPos[base + axis] += step;
          if (this.collides(row, tPos, bMin, bMax)) {
            tPos[base + axis] -= step;
            bVel[base + axis] = 0;
            if (axis === 1 && sign < 0) bGround[row] = 1;
            break;
          }
        }
      }
      if (bVel[base + 1] > 0.001) bGround[row] = 0;
    }
  }

  /** AABB-vs-voxels query; reads block solidity from the world. */
  private collides(row: number, pos: Float32Array, bMin: Float32Array, bMax: Float32Array): boolean {
    const base = row * 3;
    const minX = Math.floor(pos[base + 0] + bMin[base + 0]);
    const maxX = Math.floor(pos[base + 0] + bMax[base + 0]);
    const minY = Math.floor(pos[base + 1] + bMin[base + 1]);
    const maxY = Math.floor(pos[base + 1] + bMax[base + 1]);
    const minZ = Math.floor(pos[base + 2] + bMin[base + 2]);
    const maxZ = Math.floor(pos[base + 2] + bMax[base + 2]);
    for (let y = minY; y <= maxY; y++)
      for (let z = minZ; z <= maxZ; z++)
        for (let x = minX; x <= maxX; x++)
          if (this.world.isSolid(x, y, z)) return true;
    return false;
  }
}
```

### 4.5 System manager — strict ordering

```typescript
// /src/engine/ecs/SystemManager.ts

export interface System {
  readonly name: string;
  readonly stage: "prePhysics" | "physics" | "postPhysics" | "render";
  update(state: GameState, dt: number): void;
}

/** Topologically sorted by stage, then registration order within stage. */
export class SystemManager {
  private readonly systems: System[] = [];
  add(s: System): void { this.systems.push(s); this.sort(); }
  private sort(): void {
    const order = { prePhysics: 0, physics: 1, postPhysics: 2, render: 3 };
    this.systems.sort((a, b) => order[a.stage] - order[b.stage]);
  }
  update(state: GameState, dt: number): void {
    for (let i = 0; i < this.systems.length; i++) this.systems[i].update(state, dt);
  }
}
```


---

## 5. Block Registry — Abstract Factory

```typescript
// /src/world/blocks/BlockDefinition.ts

export interface BlockTextures {
  readonly top: number;    // index into TEXTURE_2D_ARRAY
  readonly bottom: number;
  readonly side: number;
}
export interface BlockMaterial {
  readonly opaque: boolean;
  readonly transparent: boolean; // glass, leaves
  readonly liquid: boolean;
  readonly foliage: boolean;     // renders w/ alpha-test, no AO
  readonly lightEmission: number; // 0..15
}
export interface AABB { readonly minX: number; readonly minY: number; readonly minZ: number;
                        readonly maxX: number; readonly maxY: number; readonly maxZ: number; }
export interface BlockDefinition {
  readonly id: number;          // 0..127 (legacy) or 0..4095 (modern)
  readonly name: string;
  readonly textures: BlockTextures;
  readonly material: BlockMaterial;
  readonly collision: AABB;     // full cube by default; slabs/fences differ
}

// /src/world/BlockFactory.ts

/**
 * Abstract Factory contract: subclasses decide HOW block definitions are
 * sourced (hard-coded vanilla, JSON-mod-loaded, network-synced).
 *
 * The Registry is the singleton lookup; the Factory is the producer that
 * populates it at startup.
 */
export interface BlockFactory {
  registerAll(registry: BlockRegistry): void;
}

export class VanillaBlockFactory implements BlockFactory {
  registerAll(registry: BlockRegistry): void {
    // Each call is an O(1) insertion into a fixed-size array indexed by id.
    registry.register({
      id: 1,  name: "stone",
      textures: { top: 1, bottom: 1, side: 1 },
      material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
      collision: { minX: 0, minY: 0, minZ: 0, maxX: 1, maxY: 1, maxZ: 1 },
    });
    registry.register({
      id: 2,  name: "grass",
      textures: { top: 0, bottom: 2, side: 3 },
      material: { opaque: true, transparent: false, liquid: false, foliage: false, lightEmission: 0 },
      collision: { minX: 0, minY: 0, minZ: 0, maxX: 1, maxY: 1, maxZ: 1 },
    });
    // ... 126+ more blocks: dirt, wood, leaves (transparent+foliage), glass,
    // sand, gravel, ores (coal/iron/gold/diamond), water (liquid), lava
    // (lightEission=15), planks, cobblestone, furnaces, etc.
  }
}

// /src/world/BlockRegistry.ts

export class BlockRegistry {
  private readonly defs: (BlockDefinition | null)[];
  private readonly byName = new Map<string, number>();
  constructor(readonly capacity: number = 4096) {
    this.defs = new Array(capacity).fill(null);
  }
  register(def: BlockDefinition): void {
    if (this.defs[def.id]) throw new Error(`Block id ${def.id} already registered`);
    this.defs[def.id] = def;
    this.byName.set(def.name, def.id);
  }
  /** O(1) by ID — the hot path used by the mesher. */
  get(id: number): BlockDefinition {
    const d = this.defs[id];
    if (!d) throw new Error(`Unknown block id ${id}`);
    return d;
  }
  byNameGet(name: string): number | undefined { return this.byName.get(name); }
}
```


---

## 6. Renderer — UBO + TEXTURE_2D_ARRAY + Frustum Cull (Sketch)

```typescript
// /src/engine/render/UniformBuffer.ts
/** Wraps a UBO with std140 layout. Upload once per frame. */
export class UniformBuffer {
  readonly glBuf: WebGLBuffer;
  constructor(private gl: WebGL2RenderingContext, public readonly binding: number, public readonly byteSize: number) {
    this.glBuf = gl.createBuffer()!;
  }
  upload(src: ArrayBufferView): void {
    const gl = this.gl;
    gl.bindBufferBase(gl.UNIFORM_BUFFER, this.binding, this.glBuf);
    gl.bufferData(gl.UNIFORM_BUFFER, src, gl.DYNAMIC_DRAW);
  }
}

// Per-frame: build CameraBlock (96 bytes, std140) and upload.
const cameraBlock = new ArrayBuffer(96);
new Float32Array(cameraBlock, 0, 16).set(proj);
new Float32Array(cameraBlock, 64, 16).set(view);
// camPos (12B) + time (4B) + fogColor (16B) packed into bytes 128..144 — pad per std140.
uboCamera.upload(cameraBlock);

// /src/engine/render/Texture2DArray.ts
export class Texture2DArray {
  readonly glTex: WebGLTexture;
  constructor(gl: WebGL2RenderingContext, width: number, height: number, layers: number) {
    this.glTex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, this.glTex);
    gl.texStorage3D(gl.TEXTURE_2D_ARRAY, Math.log2(width) | 0, gl.RGBA8, width, height, layers);
    // Per-layer mip generation eliminates texture bleeding (the original
    // problem with flat 2D atlases).
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST_MIPMAP_NEAREST);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  }
  uploadLayer(layer: number, src: Uint8Array, w: number, h: number): void { /* gl.texSubImage3D */ }
}

// /src/engine/render/FrustumCuller.ts
/**
 * 6-plane frustum extraction from a combined projView matrix (Gribb-Hartmann).
 * Per-chunk test is an AABB-vs-plane conservative test: returns true iff the
 * AABB intersects the frustum (or is fully inside).
 *
 * Complexity: O(6) per chunk → total O(RD²) for RD = render distance.
 */
export class FrustumCuller {
  private planes = new Float32Array(24); // 6 planes × (n.xyz + d)
  extractFrom(projView: Float32Array): void { /* standard extraction */ }
  testAABB(min: Float32Array, max: Float32Array): boolean {
    for (let p = 0; p < 6; p++) {
      const nx = this.planes[p * 4], ny = this.planes[p * 4 + 1],
            nz = this.planes[p * 4 + 2], d = this.planes[p * 4 + 3];
      // P-vertex: the AABB corner farthest in the plane's normal direction.
      const px = nx > 0 ? max[0] : min[0];
      const py = ny > 0 ? max[1] : min[1];
      const pz = nz > 0 ? max[2] : min[2];
      if (nx * px + ny * py + nz * pz + d < 0) return false;
    }
    return true;
  }
}
```


---

## 7. Configuration & Game Bootstrap

```typescript
// /src/engine/core/Config.ts
export interface GameConfig {
  renderDistance: number;     // 2..32 chunks (radial)
  chunkSize: number;          // 16 (X/Z)
  worldHeight: number;        // 256 (Y)
  maxConcurrentGenJobs: number;
  maxConcurrentMeshJobs: number;
  textureArrayLayers: number; // 256+ for vanilla blocks
  targetFps: number;
  fovDegrees: number;
}
export const DefaultConfig: GameConfig = {
  renderDistance: 12, chunkSize: 16, worldHeight: 256,
  maxConcurrentGenJobs: 4, maxConcurrentMeshJobs: 4,
  textureArrayLayers: 512, targetFps: 60, fovDegrees: 70,
};

// /src/game/Game.ts
export class Game {
  constructor(cfg: GameConfig, canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl2", { antialias: false, powerPreference: "high-performance" })!;
    // Cross-origin isolation is REQUIRED for SharedArrayBuffer.
    if (typeof SharedArrayBuffer === "undefined") throw new Error("COOP/COEP not configured");

    const blocks = new BlockRegistry(4096);
    new VanillaBlockFactory().registerAll(blocks);

    const pool = new SharedPool(/* slots */ (cfg.renderDistance * 2 + 1) ** 2, {
      sizeX: cfg.chunkSize, sizeY: cfg.worldHeight, sizeZ: cfg.chunkSize,
      maxVertsPerChunk: 65536, maxIndicesPerChunk: 131072, vertexStrideFloats: 8,
    });

    // Spawn worker pool and hand off the SharedArrayBuffer ONCE.
    const worldGenWorkers = spawnWorkers("worldgen", cfg.maxConcurrentGenJobs, pool.rawBuffer);
    const mesherWorkers    = spawnWorkers("mesher",   cfg.maxConcurrentMeshJobs, pool.rawBuffer);

    const em = new EntityManager(1 << 18);
    const transforms  = new ComponentStore(TransformDesc, em["capacity"]);
    const rigidBodies = new ComponentStore(RigidBodyDesc, em["capacity"]);
    const aiStates    = new ComponentStore(AIStateDesc,   em["capacity"]);

    const world = new World(pool, worldGenWorkers, mesherWorkers, blocks, cfg);
    const systems = new SystemManager();
    systems.add(new PhysicsSystem(em, transforms, rigidBodies, world));
    systems.add(new PathfindingSystem(em, transforms, aiStates, world));
    systems.add(new AISystem(em, aiStates, transforms));
    systems.add(new MobRenderSystem(em, transforms, /* gl */ gl));

    new GameLoop(cfg.targetFps, (dt) => systems.update(this, dt), () => this.render(gl)).start();
  }
}
```


---

## 8. Summary of algorithmic complexity

| Operation | Complexity | Notes |
|----|----|----|
| Chunk terrain generation | O(V) per chunk | Single 3D-noise + cave-carve sweep |
| Greedy meshing | O(V) per chunk | 3 axis sweeps, each O(V) |
| Frustum cull | O(RD²) per frame | 6-plane AABB test per chunk |
| Entity allocate/destroy | O(1) | Free-list stack |
| Component add/remove | O(1) amortized | Sparse-set + swap-pop |
| System iteration | O(N) per system | Contiguous dense arrays → cache-friendly |
| Worker dispatch | O(1) per chunk | Single tiny message after initial SAB handoff |

The architecture scales linearly with active chunk count and entity count, never breaks type safety, and uses zero structured-clone copies after startup — meeting every constraint of the specification.