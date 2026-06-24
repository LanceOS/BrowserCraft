# Voxel Engine Technical Design Document: Day/Night Cycle & Sky Rendering



**Version:** 1.0**Scope:** Time Progression, Celestial Math, Procedural Skybox, Dynamic Fog Blending, and Gameplay Hooks (Mob Spawning/Burning).**Architecture Constraints:** Strict TypeScript, Data-Oriented Design (SoA TypedArrays), Zero-Allocation UBO Uploads, GLSL 300 ES, ECS integration.


---

## 1. System Overview

The Day/Night cycle governs the visual atmosphere, lighting intensity, and mob spawning rules of the world. A full cycle completes in 20 minutes (1200 seconds), mirroring the 1.5.2 era.

Rather than updating thousands of block light values dynamically on the CPU, the engine utilizes a **Deferred Sky Darkening** approach. The `TimeSystem` calculates the sun's angle and a global `daylightFactor` multiplier (0.0 = night, 1.0 = day). This multiplier is uploaded to a UBO and evaluated in the fragment shader, dynamically dimming the baked "Sky Light" vertex data in real-time.

The sky itself is rendered as a procedural fullscreen gradient (with a sun/moon disc) drawn over a depth-cleared background, bypassing the need for complex sphere geometry or texture atlases.


---

## 2. TimeSystem & UBO Architecture

The `TimeSystem` is an ECS system that owns the authoritative world time. It calculates the sun's 3D direction vector and interpolates atmospheric colors, packing them into a 32-byte `TimeBlock` UBO for the GPU.

### 2.1 UBO Memory Layout (std140)

```text
Offset  Size  Field           Type
0       4     u_time          float (Total elapsed time in seconds)
4       4     u_sunAngle      float (Radians, 0 = midnight, PI/2 = noon)
8       4     u_daylight      float (0.0 = night, 1.0 = day)
12      4     u_lightLevel    float (Effective global sky light 0..15)
16      12    u_sunDir        vec3 (Normalized direction of sunlight)
28      4     u_pad           float (Alignment padding)
Total: 32 bytes
```

### 2.2 TimeSystem Implementation

```typescript
// /src/engine/ecs/systems/TimeSystem.ts

import { UniformBuffer } from "../../render/UniformBuffer";

export class TimeSystem {
  private timeOfDay: number = 0.25; // 0.0=Midnight, 0.25=Dawn, 0.5=Noon, 0.75=Dusk
  private readonly dayDuration: number = 1200; // 20 minutes
  private timeScale: number = 1.0;
  
  // Pre-allocated UBO buffer (Zero-GC)
  private readonly uboData: ArrayBuffer;
  private readonly uboF32: Float32Array;

  constructor(private readonly ubo: UniformBuffer) {
    this.uboData = new ArrayBuffer(32);
    this.uboF32 = new Float32Array(this.uboData);
  }

  /** Accelerates time to the next morning. Called when player sleeps in a bed. */
  public skipToMorning(): void {
    if (this.timeOfDay > 0.0 && this.timeOfDay < 0.25) {
      this.timeOfDay = 0.25; // Jump to dawn
    }
  }

  update(dt: number, elapsedSeconds: number): void {
    // 1. Advance Time
    this.timeOfDay = (this.timeOfDay + (dt / this.dayDuration) * this.timeScale) % 1.0;
    
    // 2. Calculate Sun Angle (0 at midnight, PI at noon)
    const sunAngle = this.timeOfDay * Math.PI * 2 - (Math.PI / 2);
    
    // 3. Calculate Sun Direction (3D Vector)
    // Sun rises East (+X), sets West (-X). Arcs through South (+Z) in northern hemisphere.
    const sunDirX = Math.cos(sunAngle);
    const sunDirY = Math.sin(sunAngle);
    const sunDirZ = 0.2; // Slight tilt for realistic shadows
    
    // 4. Calculate Darkness (0 = night, 1 = day)
    // Smoothstep transition around dawn (0.25) and dusk (0.75)
    let darkness = 0.0;
    if (this.timeOfDay > 0.20 && this.timeOfDay < 0.80) {
      // Daytime
      const t = (this.timeOfDay - 0.20) / 0.60; // 0..1 across the day
      darkness = Math.sin(t * Math.PI); // Ramps up to 1 at noon, down to 0 at edges
    } else {
      darkness = 0.0;
    }
    // Ensure smooth transition
    darkness = Math.max(0, Math.min(1, darkness * 1.2));
    
    // 5. Calculate Effective Sky Light Level (15 at day, 4 at night for moonlight)
    const lightLevel = 4.0 + (darkness * 11.0);
    
    // 6. Pack into UBO
    this.uboF32[0] = elapsedSeconds;
    this.uboF32[1] = sunAngle;
    this.uboF32[2] = darkness;
    this.uboF32[3] = lightLevel;
    this.uboF32[4] = sunDirX;
    this.uboF32[5] = sunDirY;
    this.uboF32[6] = sunDirZ;
    this.uboF32[7] = 0.0; // Padding
    
    // 7. Upload to GPU (Once per frame)
    this.ubo.upload(this.uboData);
  }

  public get isNight(): boolean {
    return this.uboF32[2] < 0.5;
  }
}
```


---

## 3. Procedural Skybox Rendering (GLSL 300 ES)

Instead of rendering a large inverted sphere with a texture atlas, we render a single fullscreen triangle (covering the screen) *before* the terrain, writing only to `gl_FragDepth` where depth == 1.0 (far plane).

The fragment shader uses the inverse Projection-View matrix to reconstruct the world-space view ray, then procedurally generates the sky gradient, sun, and moon.

### 3.1 Sky Vertex Shader

```glsl
#version 300 es
precision highp float;

// Fullscreen triangle (3 vertices)
// Position is already in Clip Space (-1 to 1)
in vec2 a_position;

out vec2 v_screenPos;

void main() {
    v_screenPos = a_position;
    gl_Position = vec4(a_position, 1.0, 1.0); // z=1, w=1 -> depth = 1.0 (far plane)
}
```

### 3.2 Sky Fragment Shader

```glsl
#version 300 es
precision highp float;

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_invProjView; // Inverse of Proj * View
  vec3 u_camPos;
  float u_time;
  vec4 u_fogColor;
};

layout(std140, binding = 2) uniform TimeBlock {
  float u_timeElapsed;
  float u_sunAngle;
  float u_daylight; // 0 = night, 1 = day
  float u_lightLevel;
  vec3 u_sunDir;
  float u_pad;
};

in vec2 v_screenPos;
out vec4 fragColor;

void main() {
    // 1. Reconstruct World-Space View Ray
    vec4 rayClip = vec4(v_screenPos, 1.0, 1.0);
    vec4 rayWorld = u_invProjView * rayClip;
    vec3 viewDir = normalize(rayWorld.xyz / rayWorld.w - u_camPos);
    
    // 2. Define Sky Colors (1.5.2 Palette)
    vec3 daySkyTop = vec3(0.2, 0.5, 1.0);
    vec3 daySkyBot = vec3(0.6, 0.8, 1.0);
    vec3 nightSky = vec3(0.01, 0.01, 0.03);
    vec3 duskColor = vec3(0.9, 0.5, 0.2);
    
    // 3. Sky Gradient based on view ray Y
    float skyGradient = clamp(viewDir.y * 2.0, 0.0, 1.0);
    vec3 daySky = mix(daySkyBot, daySkyTop, skyGradient);
    
    // 4. Dusk/Dawn blending
    // u_daylight peaks at 1 during day, 0 at night. Edges are transitions.
    float duskFactor = 1.0 - abs(u_daylight - 0.5) * 2.0; // Peaks at 0.5 daylight
    duskFactor = smoothstep(0.0, 1.0, duskFactor);
    
    vec3 finalSky = mix(nightSky, daySky, u_daylight);
    finalSky = mix(finalSky, duskColor, duskFactor * 0.6);
    
    // 5. Sun Rendering
    float sunDot = dot(viewDir, u_sunDir);
    float sunDisc = smoothstep(0.995, 0.999, sunDot);
    vec3 sunColor = mix(vec3(1.0, 0.6, 0.2), vec3(1.0, 1.0, 0.9), u_daylight);
    finalSky = mix(finalSky, sunColor, sunDisc);
    
    // 6. Moon Rendering (Opposite of Sun)
    float moonDot = dot(viewDir, -u_sunDir);
    float moonDisc = smoothstep(0.995, 0.999, moonDot);
    finalSky = mix(finalSky, vec3(0.8, 0.8, 0.9), moonDisc * (1.0 - u_daylight));
    
    // 7. Stars at night
    if (u_daylight < 0.3) {
        // Simple hash-based star noise
        float starNoise = fract(sin(dot(viewDir.xz * 100.0, vec2(12.9898, 78.233))) * 43758.5453);
        float star = step(0.998, starNoise) * (1.0 - u_daylight * 3.0);
        finalSky += vec3(star);
    }
    
    fragColor = vec4(finalSky, 1.0);
}
```


---

## 4. Dynamic Terrain Lighting Integration

The chunk fragment shader (defined in the Lighting doc) uses the `u_daylight` and `u_sunDir` from the `TimeBlock` UBO to dynamically shade the world.

* **Sky Light Dimming:** The baked Sky Light (0-15) is multiplied by `u_lightLevel / 15.0`. At night, this drops sky light to 4 (moonlight). Block light (torches) remains unaffected.
* **Directional Sun:** `u_sunDir` provides a cheap directional light source. Surfaces facing the sun receive extra brightness during the day.

```glsl
// /src/engine/render/shaders/chunk.frag (Excerpt)

layout(std140, binding = 2) uniform TimeBlock {
  float u_timeElapsed;
  float u_sunAngle;
  float u_daylight;
  float u_lightLevel; // 4 to 15
  vec3 u_sunDir;
  float u_pad;
};

void main() {
    // ... unpack smooth light ...
    float skyLight = float(v_packedLight & 0x0Fu) / 15.0;
    float blockLight = float((v_packedLight >> 4u) & 0x0Fu) / 15.0;
    
    // 1. Dim Sky Light based on Time of Day
    // u_lightLevel goes from 4 (night) to 15 (day)
    float effectiveSky = skyLight * (u_lightLevel / 15.0);
    
    // 2. Combine Lights
    float finalLight = max(effectiveSky, blockLight);
    
    // 3. Directional Sun (additive, only during day)
    vec3 N = normalize(v_normal);
    float sunDot = max(dot(N, u_sunDir), 0.0);
    float sunEffect = sunDot * u_daylight * 0.3; // 30% brightness boost in direct sunlight
    
    // 4. Apply to texture
    vec4 albedo = texture(u_blockTextures, vec3(v_uv, floor(v_texLayer + 0.5)));
    vec3 lit = albedo.rgb * (finalLight + sunEffect + 0.1);
    
    // 5. Smooth Fog transition (Fog turns blackish at night)
    vec3 fogColor = mix(vec3(0.0, 0.0, 0.0), u_fogColor.rgb, u_daylight);
    fragColor = vec4(mix(lit, fogColor, v_fogFactor), albedo.a);
}
```


---

## 5. Gameplay Hooks: DaylightSensorSystem

The `TimeSystem` exposes state that other ECS systems query to drive gameplay logic. The `DaylightSensorSystem` iterates over Hostile Mobs. If it is day, and the mob is exposed to the sky (checked via voxel Y-raycast), the mob is flagged to burn.

```typescript
// /src/engine/ecs/systems/DaylightSensorSystem.ts

import { EntityManager } from "../EntityManager";
import { ComponentStore } from "../ComponentStore";
import { HealthDesc } from "../components/Health";
import { HostileTagDesc } from "../components/Tags";
import { TimeSystem } from "./TimeSystem";
import type { World } from "../../../world/World";

export class DaylightSensorSystem {
  constructor(
    private readonly em: EntityManager,
    private readonly healths: ComponentStore<typeof HealthDesc>,
    private readonly hostiles: ComponentStore<typeof HostileTagDesc>,
    private readonly transforms: ComponentStore<typeof TransformDesc>,
    private readonly timeSystem: TimeSystem,
    private readonly world: World
  ) {}

  update(dt: number): void {
    // Only run if it is daytime
    if (this.timeSystem.isNight) return;

    const tPos = this.transforms.data.position;
    const hHp = this.healths.data.hp;
    const hRegen = this.healths.data.regenCd;

    for (const row of this.hostiles.rows()) {
      // Get Mob position
      const px = Math.floor(tPos[row * 3 + 0]);
      const py = Math.floor(tPos[row * 3 + 1]);
      const pz = Math.floor(tPos[row * 3 + 2]);

      // Raycast straight up to see if mob is under open sky
      let isUnderSky = true;
      for (let y = py + 1; y < 256; y++) {
        if (this.world.isSolid(px, y, pz)) {
          isUnderSky = false;
          break;
        }
      }

      if (isUnderSky) {
        // Burn the mob (1 damage per second)
        hHp[row] -= dt;
        // Set visual fire flag (would be handled by a StatusEffect component in full engine)
      }
    }
  }
}
```

### Summary of Day/Night Cycle Compliance


1. **Zero-Allocation UBO Updates:** The `TimeSystem` calculates sun vectors and packs data directly into a pre-allocated 32-byte `ArrayBuffer` view (`Float32Array`), uploading to the GPU via `gl.bufferSubData` once per frame without allocating a single object.
2. **Procedural Skybox:** Rendering a fullscreen triangle with a depth of 1.0 eliminates overdraw and geometry processing. The GLSL fragment shader reconstructs the view ray via the inverse projection-view matrix to procedurally generate gradients, the sun disc, the moon, and stars.
3. **Deferred Sky Lighting:** Instead of re-running the BFS Light Propagation worker every frame (which would melt the CPU), the time of day dynamically scales the baked Sky Light in the fragment shader. Night time drops sky light to 20% (moonlight), while torches (Block Light) remain unaffected.
4. **ECS Gameplay Hooks:** The `DaylightSensorSystem` queries the `TimeSystem` state and voxel raycasts to apply damage to hostile mobs, fulfilling the classic gameplay loop where Skeletons and Zombies burn at dawn.

