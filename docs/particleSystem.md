# Terrain engine Technical Design Document: Particle System



**Version:** 1.0**Scope:** CPU Particle Simulation (SoA), GPU Instanced Rendering, Block Break FX, and Ambient FX (Rain/Snow).**Architecture Constraints:** Strict TypeScript, Data-Oriented Design (SoA TypedArrays), Zero-Garbage-Collection on hot paths, WebGL2 Instanced Arrays, GLSL 300 ES.


---

## 1. System Overview

A terrain engine requires thousands of short-lived particles (block breaking debris, rain, explosions) without inducing Garbage Collection (GC) pauses or overwhelming the GPU with draw calls.

The `ParticleSystem` utilizes a **Data-Oriented Structure of Arrays (SoA)** approach. Particles are not `class` instances; they are indices into pre-allocated `Float32Array` and `Uint32Array` blocks. When a particle dies, it is swap-popped from the active array, keeping contiguous memory blocks perfectly compact for the CPU update loop and GPU upload.

Rendering is achieved via **WebGL2 Instanced Rendering**. A single 2-quad VAO is drawn `N` times per frame, where `N` is the active particle count. Per-particle data (position, color, scale) is streamed to the GPU via a single dynamic `VBO` using `gl.bufferSubData`.

**Complexity:** O(N) CPU updates and O(1) draw calls per frame, where N is the active particle count.


---

## 2. Data Structures (DOD & SoA)

To maximize CPU cache utilization, particle properties are split into distinct TypedArrays. We pre-allocate a maximum capacity (e.g., 16,384 particles) at startup.

```typescript
// /src/engine/ecs/systems/ParticleSystem.ts

import { mat4 } from "../../math/mat4";
import type { Renderer } from "../../render/Renderer";

/** Maximum active particles allocated in memory. */
const MAX_PARTICLES = 16384;

/** 
 * Particle SoA Data. 
 * We maintain a contiguous block of ACTIVE particles by swap-popping 
 * dead particles to the end of the array.
 */
export class ParticleSystem {
  // --- Per-Particle Data (Active count is `this.particleCount`) ---
  private readonly positions: Float32Array;     // x, y, z (3 per particle)
  private readonly velocities: Float32Array;    // vx, vy, vz (3 per particle)
  private readonly colors: Uint8Array;          // r, g, b, a (4 per particle, 0-255)
  private readonly sizes: Float32Array;         // scale (1 per particle)
  private readonly lives: Float32Array;         // current life (1 per particle)
  private readonly maxLives: Float32Array;      // max life for ratio calc (1 per particle)
  private readonly texLayers: Uint8Array;       // TEXTURE_2D_ARRAY slice (1 per particle)

  private particleCount: number = 0;

  // --- GPU Resources ---
  private readonly gl: WebGL2RenderingContext;
  private readonly instanceVbo: WebGLBuffer;
  private readonly vao: WebGLVertexArrayObject;
  
  // Interleaved instance buffer: [posXYZ (3f), colorRGBA (4ub), size (1f), texLayer (1ub)]
  // 12 bytes + 4 bytes + 4 bytes + 1 byte = 21 bytes. 
  // Align to 4 bytes: 24 bytes per instance.
  private readonly interleavedData: Uint8Array;
  private readonly interleavedF32: Float32Array;
  private readonly interleavedU32: Uint32Array;

  constructor(renderer: Renderer) {
    this.gl = renderer.gl;
    
    // CPU Allocations
    this.positions = new Float32Array(MAX_PARTICLES * 3);
    this.velocities = new Float32Array(MAX_PARTICLES * 3);
    this.colors = new Uint8Array(MAX_PARTICLES * 4);
    this.sizes = new Float32Array(MAX_PARTICLES);
    this.lives = new Float32Array(MAX_PARTICLES);
    this.maxLives = new Float32Array(MAX_PARTICLES);
    this.texLayers = new Uint8Array(MAX_PARTICLES);

    // Interleaved Upload Buffer (24 bytes * 16384 = 384 KiB)
    const buffer = new ArrayBuffer(MAX_PARTICLES * 24);
    this.interleavedData = new Uint8Array(buffer);
    this.interleavedF32 = new Float32Array(buffer);
    this.interleavedU32 = new Uint32Array(buffer);

    // GPU Setup: Instanced Quad VAO
    this.vao = this.gl.createVertexArray()!;
    this.gl.bindVertexArray(this.vao);

    // 1. Base Quad (2 triangles)
    const quadVbo = this.gl.createBuffer()!;
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, quadVbo);
    // 4 vertices for a 1x1 quad (left-bottom to right-top)
    const quadVerts = new Float32Array([ 
      0.0, 0.0,  1.0, 0.0,  0.0, 1.0,  1.0, 1.0 
    ]);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, quadVerts, this.gl.STATIC_DRAW);
    this.gl.enableVertexAttribArray(0);
    this.gl.vertexAttribPointer(0, 2, this.gl.FLOAT, false, 0, 0);

    // 2. Instance Buffer
    this.instanceVbo = this.gl.createBuffer()!;
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.instanceVbo);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, MAX_PARTICLES * 24, this.gl.DYNAMIC_DRAW);

    // Instance Attributes (Location 1, 2, 3)
    // Location 1: Position (vec3)
    this.gl.enableVertexAttribArray(1);
    this.gl.vertexAttribPointer(1, 3, this.gl.FLOAT, false, 24, 0);
    this.gl.vertexAttribDivisor(1, 1);

    // Location 2: Color (vec4) - packed as Uint8
    this.gl.enableVertexAttribArray(2);
    this.gl.vertexAttribPointer(2, 4, this.gl.UNSIGNED_BYTE, true, 24, 12);
    this.gl.vertexAttribDivisor(2, 1);

    // Location 3: Size (float) + TexLayer (float) 
    // We pack size and texLayer into a single vec2 for alignment efficiency
    this.gl.enableVertexAttribArray(3);
    this.gl.vertexAttribPointer(3, 2, this.gl.FLOAT, false, 24, 16);
    this.gl.vertexAttribDivisor(3, 1);

    this.gl.bindVertexArray(null);
  }
  
  // ... update and render logic below ...
}
```


---

## 3. Particle Spawning (Block Break Example)

Spawning is O(1). We write directly to the end of the active array. If the pool is full, we overwrite the oldest particle (ring-buffer behavior) or drop the spawn.

```typescript
  /**
   * Spawns a block break effect. 
   * Mirrors 1.5.2 behavior: 4-8 particles, brown/gray tinted to block texture.
   */
  public spawnBlockBreak(x: number, y: number, z: number, materialId: number, blockRegistry: MaterialRegistry): void {
    const def = blockRegistry.get(materialId);
    const count = 4 + Math.floor(Math.random() * 4);
    
    // Determine texture layer to sample color from (use side texture)
    const texLayer = def.textures.side;
    
    for (let i = 0; i < count; i++) {
      if (this.particleCount >= MAX_PARTICLES) break; // Pool full
      
      const idx = this.particleCount;
      
      // Random offset within the block volume
      this.positions[idx * 3 + 0] = x + 0.2 + Math.random() * 0.6;
      this.positions[idx * 3 + 1] = y + 0.2 + Math.random() * 0.6;
      this.positions[idx * 3 + 2] = z + 0.2 + Math.random() * 0.6;

      // Outward velocity
      this.velocities[idx * 3 + 0] = (Math.random() - 0.5) * 0.4;
      this.velocities[idx * 3 + 1] = Math.random() * 0.4 + 0.1;
      this.velocities[idx * 3 + 2] = (Math.random() - 0.5) * 0.4;

      // Color (simplified: grayish. Real engine samples texture pixel at spawn)
      this.colors[idx * 4 + 0] = 120; // R
      this.colors[idx * 4 + 1] = 120; // G
      this.colors[idx * 4 + 2] = 120; // B
      this.colors[idx * 4 + 3] = 255; // A

      this.sizes[idx] = 0.1 + Math.random() * 0.05;
      this.texLayers[idx] = texLayer;
      
      this.lives[idx] = 0.5 + Math.random() * 0.5; // 0.5 to 1.0 sec life
      this.maxLives[idx] = this.lives[idx];

      this.particleCount++;
    }
  }
```


---

## 4. CPU Simulation (Zero-GC Swap-Pop)

The update loop iterates backwards. When a particle dies, we copy the data from the *last active particle* into the dead particle's slot, then decrement `particleCount`. This avoids array shifting and keeps active data contiguous.

```typescript
  /**
   * Updates active particles. O(N) complexity. Zero allocations.
   */
  public update(dt: number, cameraRight: Float32Array, cameraUp: Float32Array): void {
    const GRAVITY = -20.0;
    const DRAG = 0.98;

    for (let i = this.particleCount - 1; i >= 0; i--) {
      // 1. Update Life
      this.lives[i] -= dt;
      if (this.lives[i] <= 0) {
        // 2. Swap-Pop: Move last particle to this index
        const last = this.particleCount - 1;
        if (i !== last) {
          // Copy 3 floats for pos/vel
          this.positions[i * 3] = this.positions[last * 3];
          this.positions[i * 3 + 1] = this.positions[last * 3 + 1];
          this.positions[i * 3 + 2] = this.positions[last * 3 + 2];
          this.velocities[i * 3] = this.velocities[last * 3];
          this.velocities[i * 3 + 1] = this.velocities[last * 3 + 1];
          this.velocities[i * 3 + 2] = this.velocities[last * 3 + 2];
          // Copy 4 bytes for color
          this.colors[i * 4] = this.colors[last * 4];
          this.colors[i * 4 + 1] = this.colors[last * 4 + 1];
          this.colors[i * 4 + 2] = this.colors[last * 4 + 2];
          this.colors[i * 4 + 3] = this.colors[last * 4 + 3];
          // Scalars
          this.sizes[i] = this.sizes[last];
          this.lives[i] = this.lives[last];
          this.maxLives[i] = this.maxLives[last];
          this.texLayers[i] = this.texLayers[last];
        }
        this.particleCount--;
        continue;
      }

      // 3. Physics Integration
      this.velocities[i * 3 + 1] += GRAVITY * dt;
      this.velocities[i * 3] *= DRAG;
      this.velocities[i * 3 + 1] *= DRAG;
      this.velocities[i * 3 + 2] *= DRAG;

      this.positions[i * 3] += this.velocities[i * 3] * dt;
      this.positions[i * 3 + 1] += this.velocities[i * 3 + 1] * dt;
      this.positions[i * 3 + 2] += this.velocities[i * 3 + 2] * dt;

      // 4. Shrink over time (classic terrain FX)
      const lifeRatio = this.lives[i] / this.maxLives[i];
      this.sizes[i] *= (0.9 + 0.1 * lifeRatio); 
    }

    // 5. Pack into interleaved buffer for GPU upload
    // We do this here so the render thread doesn't have to.
    for (let i = 0; i < this.particleCount; i++) {
      const dataOff = i * 24; // 24 bytes per particle
      
      // Pack Position (12 bytes)
      this.interleavedF32[dataOff / 4 + 0] = this.positions[i * 3];
      this.interleavedF32[dataOff / 4 + 1] = this.positions[i * 3 + 1];
      this.interleavedF32[dataOff / 4 + 2] = this.positions[i * 3 + 2];
      
      // Pack Color (4 bytes) - Uint8 view handles byte alignment automatically
      this.interleavedData[dataOff + 12] = this.colors[i * 4];
      this.interleavedData[dataOff + 13] = this.colors[i * 4 + 1];
      this.interleavedData[dataOff + 14] = this.colors[i * 4 + 2];
      this.interleavedData[dataOff + 15] = this.colors[i * 4 + 3];

      // Pack Size and TexLayer (8 bytes)
      this.interleavedF32[dataOff / 4 + 4] = this.sizes[i];
      this.interleavedF32[dataOff / 4 + 5] = this.texLayers[i];
    }
  }
```


---

## 5. GPU Instanced Rendering

The render call issues a **single draw command** for all particles. The GPU handles the billboarding math via the vertex shader.

```typescript
  /**
   * Renders all active particles. O(1) draw calls.
   */
  public render(): void {
    if (this.particleCount === 0) return;
    const gl = this.gl;

    // 1. Upload Interleaved Data
    gl.bindBuffer(gl.ARRAY_BUFFER, this.instanceVbo);
    // Only upload the active portion of the buffer
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.interleavedData, 0, this.particleCount * 24);

    // 2. Bind VAO and Draw
    gl.bindVertexArray(this.vao);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.disable(gl.CULL_FACE); // Particles are double-sided
    
    // Base quad has 4 vertices. We draw triangles as a triangle strip.
    // instancedCount = this.particleCount
    gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, this.particleCount);
    
    gl.enable(gl.CULL_FACE);
    gl.bindVertexArray(null);
  }
```


---

## 6. GLSL 300 ES Shaders (Billboarding)

The vertex shader uses the Camera's Right and Up vectors (passed via UBO) to construct a camera-facing billboard quad from the 2D base coordinates.

```glsl
#version 300 es
precision highp float;

// Global Camera UBO
layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  vec3 u_camPos;
  float u_time;
  vec4 u_fogColor;
  // Custom addition for particles: camera basis vectors
  vec3 u_camRight;
  vec3 u_camUp;
};

// Base Quad Vertices (0.0 to 1.0)
in vec2 a_pos;

// Instance Attributes
in vec3 i_position;
in vec4 i_color;
in vec2 i_size_tex; // x = size, y = texLayer

out vec2 v_uv;
out vec4 v_color;
out float v_texLayer;
out float v_fogFactor;

void main() {
  // 1. Calculate Billboard World Position
  // a_pos is [0,1]. We center it [-0.5, 0.5] and scale by i_size
  float scale = i_size_tex.x;
  vec3 billboardOffset = (u_camRight * (a_pos.x - 0.5) + u_camUp * (a_pos.y - 0.5)) * scale;
  
  vec3 worldPos = i_position + billboardOffset;
  vec4 clipPos = u_projView * vec4(worldPos, 1.0);
  
  gl_Position = clipPos;

  // 2. Pass data to Fragment Shader
  v_uv = a_pos;
  v_color = i_color;
  v_texLayer = i_size_tex.y;

  // 3. Fog Calculation
  float dist = length(u_camPos - worldPos);
  v_fogFactor = clamp(1.0 - (u_fogColor.a - dist) / u_fogColor.a, 0.0, 1.0);
}
```

```glsl
#version 300 es
precision highp float;

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj; mat4 u_view; mat4 u_projView;
  vec3 u_camPos; float u_time; vec4 u_fogColor;
  vec3 u_camRight; vec3 u_camUp;
};

uniform sampler2DArray u_blockTextures;

in vec2 v_uv;
in vec4 v_color;
in float v_texLayer;
in float v_fogFactor;

out vec4 fragColor;

void main() {
  // 1. Sample from Texture Array
  vec4 albedo = texture(u_blockTextures, vec3(v_uv, floor(v_texLayer + 0.5)));
  
  // 2. Tint by particle color (used for tinted glass, mob blood, etc.)
  albedo *= v_color;

  // Discard transparent pixels for alpha-test (prevents depth write issues)
  if (albedo.a < 0.1) discard;

  // 3. Apply Fog
  fragColor = vec4(mix(albedo.rgb, u_fogColor.rgb, v_fogFactor), albedo.a);
}
```

### Summary of Particle System Compliance


1. **Zero-GC SoA Design:** Particles are indices into pre-allocated `Float32Array`s. Dead particles are swap-popped to the end of the array, maintaining contiguous memory blocks and avoiding array shifts or `splice()` calls.
2. **Instanced Rendering:** Reduces 10,000 particle draws to exactly **one** `drawArraysInstanced` call. The base quad VBO is 32 bytes; the per-instance data is streamed via a single `bufferSubData` call per frame.
3. **GPU Billboarding:** The vertex shader leverages `u_camRight` and `u_camUp` vectors injected into the global UBO to construct camera-facing quads. This offloads matrix math to the GPU.
4. **Texture Array Integration:** Particles sample from the same `TEXTURE_2D_ARRAY` as the chunk terrain, allowing block-break particles to accurately use the broken block's texture slice.


