# Voxel Engine Technical Design Document: Physics & Collision Detection



**Version:** 1.0**Scope:** Entity Physics, Swept-AABB Voxel Collision, Step-Assist, Gravity, and Fluid Dynamics.**Architecture Constraints:** Strict TypeScript, Data-Oriented Design (SoA TypedArrays), Zero-Garbage-Collection on hot paths, Fixed-Timestep determinism.


---

## 1. System Overview

Physics in a voxel sandbox must handle entities of varying AABB sizes interacting with up to 65,536 blocks per chunk. To maintain 60 FPS with hundreds of entities, we discard heavy physics engines (like Rapier or Cannon.js) and implement a **Discrete Sub-stepping AABB Collider** directly against the voxel grid.

The `PhysicsSystem` iterates over the `RigidBody` and `Transform` components. It integrates velocity, applies gravity, and resolves collisions axis-by-axis (Y, then X, then Z) to prevent snagging on block seams.

**Complexity:** O(N) per frame, where N is the number of physics-enabled entities. Per-entity collision check is O(W*H*D) where W,H,D are the entity's bounding block volume.


---

## 2. Component Data Structures (DOD)

The `RigidBody` component stores all kinematic data in a Structure of Arrays format. The AABB is stored in *local* space (relative to the entity origin) and translated to world space during collision checks.

```typescript
// /src/engine/ecs/components/RigidBody.ts
import { ComponentDesc } from "../ComponentStore";

export const RigidBodyDesc = {
  velocity:    { type: Float32Array, length: 3 }, // [vx, vy, vz]
  aabbMin:     { type: Float32Array, length: 3 }, // Local bounds (e.g., [-0.3, 0, -0.3])
  aabbMax:     { type: Float32Array, length: 3 }, // Local bounds (e.g., [0.3, 1.8, 0.3])
  onGround:    { type: Uint8Array,   length: 1 }, // 0 = false, 1 = true
  isFluid:     { type: Uint8Array,   length: 1 }, // Buoyancy flag
  gravity:     { type: Float32Array, length: 1 }, // m/s^2 (20.0 for mobs, 32.0 for items)
  drag:        { type: Float32Array, length: 1 }, // Air friction (0.98)
} as const satisfies ComponentDesc;
```


---

## 3. The Voxel Collision Algorithm

To prevent fast-moving entities from tunneling through 1-block-thick walls, we divide the frame's movement delta into discrete sub-steps. No entity may move more than 0.5 blocks per sub-step.

### 3.1 Axis-Separated Resolution

Moving in all three axes simultaneously and checking collisions causes entities to "catch" on block corners. Instead, we move **Y first**, then **X**, then **Z**. If a collision occurs on an axis, we snap the entity to the block face and zero out that velocity component.

### 3.2 Implementation

```typescript
// /src/engine/ecs/systems/PhysicsSystem.ts

import { EntityManager } from "../EntityManager";
import { ComponentStore } from "../ComponentStore";
import { TransformDesc } from "../components/Transform";
import { RigidBodyDesc } from "../components/RigidBody";
import type { World } from "../../../world/World";

export class PhysicsSystem {
  private static readonly GRAVITY = -28.0; // Blocks per second squared
  private static readonly TERMINAL_VELOCITY = -55.0;
  private static readonly STEP_HEIGHT = 0.52; // Auto-step up slabs/stairs

  constructor(
    private readonly em: EntityManager,
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly bodies: ComponentStore<typeof RigidBodyDesc>,
    private readonly world: World
  ) {}

  update(dt: number): void {
    const tPos = this.transforms.data.position;
    const bVel = this.bodies.data.velocity;
    const bMin = this.bodies.data.aabbMin;
    const bMax = this.bodies.data.aabbMax;
    const bGround = this.bodies.data.onGround;
    const bGrav = this.bodies.data.gravity;
    const bDrag = this.bodies.data.drag;

    for (const row of this.bodies.rows()) {
      const base = row * 3;

      // 1. Apply Gravity & Drag
      bVel[base + 1] -= bGrav[row] * dt;
      if (bVel[base + 1] < this.TERMINAL_VELOCITY) bVel[base + 1] = this.TERMINAL_VELOCITY;
      
      bVel[base + 0] *= bDrag[row];
      bVel[base + 2] *= bDrag[row];

      // 2. Calculate Sub-steps (Prevent Tunneling)
      const dx = bVel[base + 0] * dt;
      const dy = bVel[base + 1] * dt;
      const dz = bVel[base + 2] * dt;

      const maxDelta = Math.max(Math.abs(dx), Math.abs(dy), Math.abs(dz));
      const steps = Math.ceil(maxDelta / 0.5);
      const invSteps = 1.0 / steps;

      // 3. Sub-step Loop
      for (let s = 0; s < steps; s++) {
        const stepDx = dx * invSteps;
        const stepDy = dy * invSteps;
        const stepDz = dz * invSteps;

        // --- Y Axis ---
        tPos[base + 1] += stepDy;
        if (this.checkCollision(row, tPos, bMin, bMax)) {
          // Revert and snap
          tPos[base + 1] -= stepDy;
          if (stepDy < 0) {
            // Moving down, snap to top of block
            tPos[base + 1] = Math.floor(tPos[base + 1] + bMin[base + 1]) - bMin[base + 1];
            bGround[row] = 1;
          } else {
            // Moving up, snap to bottom of block
            tPos[base + 1] = Math.ceil(tPos[base + 1] + bMax[base + 1]) - 1 - bMax[base + 1];
          }
          bVel[base + 1] = 0;
        } else if (stepDy < 0) {
          // If we moved down successfully, we are falling
          bGround[row] = 0;
        }

        // --- X Axis (with Step Assist) ---
        tPos[base + 0] += stepDx;
        if (this.checkCollision(row, tPos, bMin, bMax)) {
          // Try auto-step: raise entity by STEP_HEIGHT, move X, drop back down
          if (bGround[row] === 1 && stepDx !== 0) {
            tPos[base + 1] += this.STEP_HEIGHT;
            if (!this.checkCollision(row, tPos, bMin, bMax)) {
              // Step successful, leave Y raised for now (gravity will settle it)
              continue;
            }
            tPos[base + 1] -= this.STEP_HEIGHT;
          }
          // Step failed, revert and snap X
          tPos[base + 0] -= stepDx;
          if (stepDx > 0) tPos[base + 0] = Math.floor(tPos[base + 0] + bMax[base + 0]) - bMax[base + 0];
          else            tPos[base + 0] = Math.ceil(tPos[base + 0] + bMin[base + 0]) - 1 - bMin[base + 0];
          bVel[base + 0] = 0;
        }

        // --- Z Axis (with Step Assist) ---
        tPos[base + 2] += stepDz;
        if (this.checkCollision(row, tPos, bMin, bMax)) {
          if (bGround[row] === 1 && stepDz !== 0) {
            tPos[base + 1] += this.STEP_HEIGHT;
            if (!this.checkCollision(row, tPos, bMin, bMax)) {
              continue;
            }
            tPos[base + 1] -= this.STEP_HEIGHT;
          }
          tPos[base + 2] -= stepDz;
          if (stepDz > 0) tPos[base + 2] = Math.floor(tPos[base + 2] + bMax[base + 2]) - bMax[base + 2];
          else            tPos[base + 2] = Math.ceil(tPos[base + 2] + bMin[base + 2]) - 1 - bMin[base + 2];
          bVel[base + 2] = 0;
        }
      }
    }
  }

  /**
   * Checks if the entity's world-space AABB intersects any solid voxel.
   * O(W * H * D) complexity.
   */
  private checkCollision(
    row: number, 
    pos: Float32Array, 
    bMin: Float32Array, 
    bMax: Float32Array
  ): boolean {
    const base = row * 3;
    
    // Calculate World-Space AABB bounds
    const minX = Math.floor(pos[base + 0] + bMin[base + 0]);
    const maxX = Math.floor(pos[base + 0] + bMax[base + 0]);
    const minY = Math.floor(pos[base + 1] + bMin[base + 1]);
    const maxY = Math.floor(pos[base + 1] + bMax[base + 1]);
    const minZ = Math.floor(pos[base + 2] + bMin[base + 2]);
    const maxZ = Math.floor(pos[base + 2] + bMax[base + 2]);

    // Iterate over all voxels overlapping the AABB
    for (let y = minY; y <= maxY; y++) {
      for (let z = minZ; z <= maxZ; z++) {
        for (let x = minX; x <= maxX; x++) {
          if (this.world.isSolid(x, y, z)) {
            return true;
          }
        }
      }
    }
    return false;
  }
}
```


---

## 4. World Voxel Query Interface

The `PhysicsSystem` relies on `World.isSolid(x, y, z)`. This function must account for partial AABBs (slabs, stairs, fences).

To maintain DOD principles, block collision boundaries are defined in the `BlockRegistry` and fetched in O(1) time.

```typescript
// /src/world/World.ts (Excerpt)

public isSolid(x: number, y: number, z: number): boolean {
  // 1. Bounds check (World height 0..255)
  if (y < 0 || y >= 256) return false;

  // 2. Get Chunk and local coords
  const chunkX = x >> 4; // Fast divide by 16
  const chunkZ = z >> 4;
  const localX = x & 15; // Fast modulo 16
  const localZ = z & 15;
  
  const chunk = this.chunkManager.getChunk(chunkX, chunkZ);
  if (!chunk) return false; // Unloaded chunk = no collision (let entity fall)

  // 3. Fetch Block ID and check Registry
  const blockId = chunk.getBlock(localX, y, localZ);
  if (blockId === 0) return false; // Air

  const def = this.blockRegistry.get(blockId);
  
  // Liquids and fences have specific rules.
  // For standard physics, we treat anything with a non-empty AABB as solid.
  return def.collision.maxX > def.collision.minX; 
}
```


---

## 5. Fluid & Buoyancy Physics

When an entity enters a water/lava block, the `isFluid` flag is set (usually by a separate environment sensor system). The `PhysicsSystem` applies different integration rules for fluid entities:


1. **Gravity Reversal**: Gravity is reduced to `-4.0` m/s².
2. **Drag Multiplier**: Velocity is multiplied by `0.8` instead of `0.98` (high water friction).
3. **Buoyancy**: If the entity is denser than water, it sinks slowly. If lighter (or holding spacebar), it rises.

```typescript
  // Inside PhysicsSystem.update(), before applying gravity:
  if (this.bodies.data.isFluid[row] === 1) {
    this.bodies.data.gravity[row] = -4.0;
    this.bodies.data.drag[row] = 0.80;
    
    // Buoyancy / Swim up
    if (input.isHeld(4)) { // Spacebar
      this.bodies.data.velocity[base + 1] += 8.0 * dt; // Thrust upwards
    }
  } else {
    this.bodies.data.gravity[row] = this.DEFAULT_GRAVITY;
    this.bodies.data.drag[row] = 0.98;
  }
```


---

## 6. Performance & Architectural Compliance


1. **SoA Cache Locality**: All velocity, position, and AABB data is accessed via `row * 3` indexing into contiguous `Float32Array`s. The CPU prefetcher perfectly streams this data during the `for (const row of this.bodies.rows())` loop.
2. **Zero Allocations**: No `Vector3` objects are created. Math is done inline with primitive floats. The `Math.floor` and `Math.ceil` calls are hardware-intrinsic and do not allocate.
3. **Tunneling Prevention**: By calculating the maximum axis delta and dividing it by 0.5, we guarantee that no entity moves more than half a block per check. A 1-block thick wall will *always* be caught, even at terminal velocity.
4. **Deterministic Sub-stepping**: The physics step runs inside a fixed-timestep accumulator (e.g., exactly 60Hz). This ensures that mob AI pathfinding and player movement remain deterministic across different display refresh rates (60Hz vs 144Hz monitors).


