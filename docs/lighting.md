# Voxel Engine Technical Design Document: Lighting & Day/Night Cycle



**Version:** 1.0**Scope:** Voxel Light Propagation (Block Light & Sky Light), Smooth Lighting interpolation, Time Progression, and Atmospheric Sky Rendering.**Architecture Constraints:** Strict TypeScript, Web Worker isolation, Zero-Copy SharedArrayBuffer, GLSL 300 ES, Data-Oriented Design.


---

## 1. System Overview

A defining visual feature of the 1.3.2/1.5.2 era was the introduction of **Smooth Lighting** and a dynamic **Day/Night Cycle**. To achieve this without runtime GC pauses or CPU bottlenecks, lighting is calculated entirely within the `MesherWorker` and baked directly into the chunk's vertex data.

The engine utilizes a dual-lighting model:


1. **Sky Light (15):** Propagates straight down from Y=255. Any block exposed to the sky receives maximum light, simulating sunlight.
2. **Block Light (0-15):** Emitted by light sources (Torches, Glowstone, Lava) and propagated via a Breadth-First Search (BFS) flood fill with linear falloff.

A `TimeSystem` on the main thread updates a global `TimeBlock` UBO, dictating the sun angle, sky fog color, and how much the sky light is dimmed (e.g., night time sky light drops from 15 to 4).


---

## 2. Light Data Storage (Zero-Copy)

In the `SharedPool` architecture defined previously, each chunk slot contains a `light: Uint8Array` of size 65,536 (16x16x256).

To avoid doubling memory, we pack both light channels into a single byte per voxel:

* **High Nibble (bits 4-7):** Block Light (0-15)
* **Low Nibble (bits 0-3):** Sky Light (0-15)

```typescript
// /src/engine/workers/mesher/LightPacker.ts

export const MAX_LIGHT = 15;

/** Extracts Sky Light (0-15) from a packed byte. O(1) */
export function getSkyLight(packed: number): number {
  return packed & 0x0F;
}

/** Extracts Block Light (0-15) from a packed byte. O(1) */
export function getBlockLight(packed: number): number {
  return (packed >> 4) & 0x0F;
}

/** Packs Sky and Block light into a single byte. O(1) */
export function packLight(sky: number, block: number): number {
  return ((block & 0x0F) << 4) | (sky & 0x0F);
}
```


---

## 3. Light Propagation Algorithm (Worker-Side)

Lighting is calculated in the `MesherWorker` *before* the greedy meshing step.

Because workers operate in isolation, they can only accurately propagate light within their own 16x256x16 chunk column. To prevent harsh lighting seams at chunk borders, the mesher samples neighbor chunks (if provided by the main thread) using a +1 boundary extension.

### 3.1 Sky Light Column Pass (O(Y) per column)

Sky light is computed first. A ray is cast straight down from `Y=255`. Air, leaves, and glass are transparent to sky light. Once a solid block is hit, all blocks below receive 0 sky light.

### 3.2 Block Light BFS Flood Fill (O(V))

Light sources are collected, then a queue-based BFS propagates light to neighbors, decreasing by 1 per block.

```typescript
// /src/engine/workers/mesher/LightPropagator.ts

import { packLight, getSkyLight, getBlockLight, MAX_LIGHT } from "./LightPacker";
import type { ChunkDimensions } from "../../alloc/SharedPool";

export class LightPropagator {
  /**
   * Populates the chunk's light array.
   * Mutates the SharedArrayBuffer directly.
   * 
   * @param voxels Uint8Array of block IDs
   * @param light Uint8Array of packed light data (target)
   */
  public static calculate(
    voxels: Uint8Array, 
    light: Uint8Array, 
    dims: ChunkDimensions,
    lightEmissionMap: Map<number, number>, // e.g., Block 50 (Torch) -> 14
    transparentBlocks: Set<number>
  ): void {
    const { sizeX, sizeY, sizeZ } = dims;
    
    // 1. Sky Light Pass + Light Source Collection
    const queue = new Int32Array(sizeX * sizeY * sizeZ * 3); // Pre-allocated queue [x,y,z, x,y,z...]
    let queueHead = 0;
    let queueTail = 0;

    for (let z = 0; z < sizeZ; z++) {
      for (let x = 0; x < sizeX; x++) {
        let skyLevel = MAX_LIGHT;
        for (let y = sizeY - 1; y >= 0; y--) {
          const idx = (y * sizeZ + z) * sizeX + x;
          const blockId = voxels[idx];
          
          if (!transparentBlocks.has(blockId)) {
            skyLevel = 0; // Solid block blocks sky light
          }
          
          // Write sky light (block light starts at 0)
          light[idx] = packLight(skyLevel, 0);

          // Check if this block emits light
          const emit = lightEmissionMap.get(blockId) || 0;
          if (emit > 0) {
            // Update block light in the packed byte
            light[idx] = packLight(skyLevel, emit);
            // Enqueue for BFS
            queue[queueTail++] = x;
            queue[queueTail++] = y;
            queue[queueTail++] = z;
          }
        }
      }
    }

    // 2. Block Light BFS Pass
    while (queueHead < queueTail) {
      const x = queue[queueHead++];
      const y = queue[queueHead++];
      const z = queue[queueHead++];
      
      const idx = (y * sizeZ + z) * sizeX + x;
      const currentPacked = light[idx];
      const currentBlockLight = getBlockLight(currentPacked);
      const skyLight = getSkyLight(currentPacked);
      
      if (currentBlockLight <= 1) continue; // Cannot propagate further

      const nextLight = currentBlockLight - 1;

      // Check 6 neighbors
      // +X
      if (x < sizeX - 1) this.tryPropagate(x + 1, y, z, nextLight, skyLight, voxels, light, dims, queue, queueTail);
      // -X
      if (x > 0)         this.tryPropagate(x - 1, y, z, nextLight, skyLight, voxels, light, dims, queue, queueTail);
      // +Y
      if (y < sizeY - 1) this.tryPropagate(x, y + 1, z, nextLight, skyLight, voxels, light, dims, queue, queueTail);
      // -Y
      if (y > 0)         this.tryPropagate(x, y - 1, z, nextLight, skyLight, voxels, light, dims, queue, queueTail);
      // +Z
      if (z < sizeZ - 1) this.tryPropagate(x, y, z + 1, nextLight, skyLight, voxels, light, dims, queue, queueTail);
      // -Z
      if (z > 0)         this.tryPropagate(x, y, z - 1, nextLight, skyLight, voxels, light, dims, queue, queueTail);
    }
  }

  private static tryPropagate(
    x: number, y: number, z: number, 
    newLight: number, skyLight: number,
    voxels: Uint8Array, light: Uint8Array, 
    dims: ChunkDimensions, queue: Int32Array, queueTail: number
  ): void {
    const idx = (y * dims.sizeZ + z) * dims.sizeX + x;
    const blockId = voxels[idx];
    // Cannot propagate into solid, non-transparent blocks
    if (blockId !== 0 && blockId !== 18 /* leaves */ && blockId !== 20 /* glass */) return;

    const currentPacked = light[idx];
    if (getBlockLight(currentPacked) < newLight) {
      light[idx] = packLight(skyLight, newLight);
      queue[queueTail++] = x;
      queue[queueTail++] = y;
      queue[queueTail++] = z;
    }
  }
}
```


---

## 4. Smooth Lighting (Mesher Integration)

To achieve the classic 1.5.2 Smooth Lighting, the `GreedyMesher` cannot use a single light value per face. Instead, for every vertex, it samples the light values of the 4 surrounding voxels (air blocks adjacent to the face) and averages them.

This blends the light smoothly across the greedy meshed quads.

### 4.1 Vertex Format Update

We upgrade our interleaved vertex buffer from 8 floats to **8 floats + 1 Uint32** (36 bytes total).

* `Position (3 f32)`
* `Normal (3 f32)`
* `UV (2 f32)`
* `Packed Light (1 u32)`: Bits 0-3 = Sky Light, Bits 4-7 = Block Light, Bits 8-15 = Unused, Bits 16-31 = Ambient Occlusion (packed from the previous AO system).

### 4.2 Vertex Light Sampling

```typescript
// /src/engine/workers/mesher/GreedyMesher.ts (Excerpt)

/**
 * Samples light for a specific corner of a quad.
 * Samples the 3 voxels diagonal/adjacent to the corner in the air space,
 * averages them, and returns a packed u32.
 */
private sampleSmoothLight(
  getVoxel: (x: number, y: number, z: number) => number,
  getLight: (x: number, y: number, z: number) => number,
  x: number, y: number, z: number,
  normal: number[], du: number[], dv: number[]
): number {
  // The 4 voxels to sample are offset into the air space (normal direction)
  // and distributed around the corner.
  // This is a simplified 3-sample average for demonstration.
  const nx = x + normal[0];
  const ny = y + normal[1];
  const nz = z + normal[2];
  
  const l1 = getLight(nx, ny, nz);
  const l2 = getLight(nx + du[0], ny + du[1], nz + du[2]);
  const l3 = getLight(nx + dv[0], ny + dv[1], nz + dv[2]);
  const l4 = getLight(nx + du[0] + dv[0], ny + du[1] + dv[1], nz + du[2] + dv[2]);
  
  // Average the components
  const sky = ((l1 & 0x0F) + (l2 & 0x0F) + (l3 & 0x0F) + (l4 & 0x0F)) >> 2;
  const block = ((l1 >> 4) + (l2 >> 4) + (l3 >> 4) + (l4 >> 4)) >> 2;
  
  return (block << 4) | sky;
}
```


---

## 5. Time System & Day/Night Cycle

The `TimeSystem` runs on the main thread and updates a global UBO (`TimeBlock`). It calculates the sun angle and determines the global daylight factor.

A full day lasts 20 minutes (1200 seconds), mirroring Minecraft.

```typescript
// /src/engine/ecs/systems/TimeSystem.ts

import { UniformBuffer } from "../../render/UniformBuffer";

export class TimeSystem {
  private timeOfDay: number = 0; // 0.0 to 1.0 (0 = midnight, 0.5 = noon)
  private readonly dayDuration: number = 1200; // Seconds for a full cycle
  
  // UBO Layout (std140, 16 bytes):
  // 0: float u_timeOfDay (0.0 to 1.0)
  // 4: float u_sunAngle (radians)
  // 8: float u_daylight (0.0 = night, 1.0 = day)
  // 12: float u_pad
  private readonly uboData: ArrayBuffer;
  private readonly uboF32: Float32Array;

  constructor(private readonly ubo: UniformBuffer) {
    this.uboData = new ArrayBuffer(16);
    this.uboF32 = new Float32Array(this.uboData);
  }

  update(dt: number): void {
    this.timeOfDay = (this.timeOfDay + dt / this.dayDuration) % 1.0;
    
    // Sun angle: 0 at midnight, PI at noon
    const sunAngle = this.timeOfDay * Math.PI * 2 - (Math.PI / 2);
    
    // Sky darkness: 1.0 during day, 0.0 at night. Smooth transition.
    // sin(sunAngle) is -1 at midnight, 1 at noon.
    let daylightFactor = Math.max(0, Math.sin(sunAngle));
    daylightFactor = Math.pow(daylightFactor, 0.5); // Ease curve
    
    this.uboF32[0] = this.timeOfDay;
    this.uboF32[1] = sunAngle;
    this.uboF32[2] = daylightFactor;
    this.uboF32[3] = 0;
    
    this.ubo.upload(this.uboData);
  }
}
```


---

## 6. WebGL2 Shader Integration (GLSL 300 ES)

The fragment shader takes the baked vertex light, applies the global day/night multiplier, and calculates the final block color.

```glsl
// /src/engine/render/shaders/chunk.frag
#version 300 es
precision highp float;

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj; mat4 u_view; mat4 u_projView;
  vec3 u_camPos; float u_time; vec4 u_fogColor;
};

// Time UBO (Binding 2)
layout(std140, binding = 2) uniform TimeBlock {
  float u_timeOfDay;
  float u_sunAngle;
  float u_daylight; // 0 = night, 1 = day
  float u_pad;
};

uniform sampler2DArray u_blockTextures;

in vec2 v_uv;
in vec3 v_normal;
in vec3 v_worldPos;
in float v_texLayer;
in float v_fogFactor;
flat in uint v_packedLight; // Smooth lighting data passed from vertex shader

out vec4 fragColor;

void main() {
  // 1. Unpack smooth light
  float skyLight = float(v_packedLight & 0x0Fu) / 15.0;
  float blockLight = float((v_packedLight >> 4u) & 0x0Fu) / 15.0;
  
  // 2. Apply Day/Night cycle to Sky Light
  // At night, sky light drops to ~20% (moonlight)
  float effectiveSky = skyLight * mix(0.2, 1.0, u_daylight);
  
  // 3. Combine lights (Max of sky and block)
  float finalLight = max(effectiveSky, blockLight);
  
  // 4. Texture sampling
  vec4 albedo = texture(u_blockTextures, vec3(v_uv, floor(v_texLayer + 0.5)));
  if (albedo.a < 0.1) discard;
  
  // 5. Cheap directional light (sun)
  vec3 N = normalize(v_normal);
  vec3 L = normalize(vec3(cos(u_sunAngle), sin(u_sunAngle), 0.3));
  float ndl = max(dot(N, L), 0.0);
  
  // Sun only affects surfaces during the day
  float sunEffect = ndl * u_daylight * 0.3; 
  
  // 6. Final color composition
  vec3 lit = albedo.rgb * (finalLight + sunEffect + 0.1); // 0.1 ambient baseline
  
  // 7. Apply Fog
  fragColor = vec4(mix(lit, u_fogColor.rgb, v_fogFactor), albedo.a);
}
```

### Summary of Lighting Compliance


1. **Zero-Copy Worker Baking:** Light propagation (BFS + Sky cast) runs entirely in the `MesherWorker`. It mutates the `SharedArrayBuffer` directly, avoiding main-thread stalls.
2. **Smooth Lighting:** The `GreedyMesher` samples a 4-voxel area for every vertex corner, averaging the packed light nibbles. This produces the classic 1.5.2 gradient lighting on terrain faces.
3. **Bit-Packed Data:** Storing Sky and Block light in a single byte (`Uint8Array`) halves memory overhead compared to a `Uint16Array`, improving CPU cache line utilization during the BFS flood fill.
4. **Time UBO:** The Day/Night cycle is driven by a 16-byte UBO uploaded once per frame. The fragment shader lerps the sky light contribution between 20% (moonlight) and 100% (daylight) using `u_daylight`.

