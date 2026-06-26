#pragma once

namespace voxel::shaders {

// OpenGL 4.6 core GLSL — ported from WebGL2

inline const char* chunkVertex = R"glsl(#version 460 core

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  mat4 u_invProjView;
  vec4 u_camTime;
  vec4 u_fogColor;
  vec4 u_camRight;
  vec4 u_camUp;
};

layout(std140, binding = 2) uniform TimeBlock {
  float u_timeElapsed;
  float u_daylight;
  float u_sunIntensity;
  float u_ambientIntensity;
  vec3  u_sunDir;
  float u_timeOfDay;
  vec3  u_sunColor;
  float u_pad;
};

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in float a_texLayer;
layout(location = 4) in float a_lightData;

struct ChunkCullData {
    vec4 min;
    vec4 max;
    uint indexCount;
    uint firstIndex;
    uint baseVertex;
    uint slotIndex;
    uint hasOpaque;
    uint hasTransparent;
    uint pad1;
    uint pad2;
};

layout(std430, binding = 0) readonly buffer InputChunks {
    ChunkCullData chunks[];
};

out vec2 v_uv;
out vec3 v_normal;
out float v_texLayer;
out vec3 v_worldPos;
out float v_skyLight;
out float v_blockLight;
out float v_ao;

void main() {
  // @see notes/indirect-draw-base-instance.md
  uint chunkIndex = gl_BaseInstance;
  vec3 chunkTranslation = chunks[int(chunkIndex)].min.xyz;
  vec3 worldPos = a_pos + chunkTranslation;
  vec4 clip = u_projView * vec4(worldPos, 1.0);
  gl_Position = clip;

  v_uv = a_uv;
  v_normal = a_normal;
  v_texLayer = a_texLayer;
  v_worldPos = worldPos;
  uint packedLight = uint(a_lightData + 0.5);
  v_skyLight = float(packedLight & 0x0Fu) / 15.0;
  v_blockLight = float((packedLight >> 4u) & 0x0Fu) / 15.0;
  v_ao = float((packedLight >> 16u) & 0x03u) / 3.0;
}
)glsl";

inline const char* chunkFragment = R"glsl(#version 460 core

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  mat4 u_invProjView;
  vec4 u_camTime;
  vec4 u_fogColor;
  vec4 u_camRight;
  vec4 u_camUp;
};

layout(std140, binding = 2) uniform TimeBlock {
  float u_timeElapsed;
  float u_daylight;
  float u_sunIntensity;
  float u_ambientIntensity;
  vec3  u_sunDir;
  float u_timeOfDay;
  vec3  u_sunColor;
  float u_pad;
};

uniform sampler2DArray u_blockTextures;
uniform int u_opaquePass;

in vec2 v_uv;
in vec3 v_normal;
in float v_texLayer;
in vec3 v_worldPos;
in float v_skyLight;
in float v_blockLight;
in float v_ao;

out vec4 fragColor;

void main() {
  vec4 albedo = texture(u_blockTextures, vec3(v_uv, floor(v_texLayer + 0.5)));
  if (albedo.a < 0.05) {
    discard;
  }

  if (u_opaquePass == 1 && albedo.a < 0.5) {
    discard;
  }
  if (u_opaquePass == 0 && albedo.a >= 0.5) {
    discard;
  }

  vec3 normal = normalize(v_normal);
  vec3 sunDir = normalize(u_sunDir);
  float skyLight = v_skyLight * mix(0.18, 1.0, u_daylight);
  float blockLight = v_blockLight;
  float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
  float lightPresence = max(v_skyLight, v_blockLight);

  // @see notes/chunk-shadow-banding.md
  // Keep AO as an ambient term so the sun stays directional and nearby
  // occluders read as soft shading instead of hard banded stripes.
  // Use the baked light field as the primary signal so adjacent faces blend
  // smoothly, then layer a softer directional sun over exposed surfaces.
  float ambient = mix(0.04, u_ambientIntensity * mix(0.16, 0.36, upward), lightPresence);
  float indirect = max(skyLight, blockLight);
  float overlap = 0.20 * skyLight + 0.30 * blockLight;
  float sunMask = smoothstep(0.05, 0.85, v_skyLight);
  float sunDiffuse = max(dot(normal, sunDir), 0.0) * u_sunIntensity * sunMask * 0.35;
  float aoFactor = 0.75 + v_ao * 0.25;

  vec3 lighting = vec3(ambient + indirect * 0.50 + overlap * 0.20) * aoFactor
                + u_sunColor * sunDiffuse;
  lighting = max(lighting, vec3(0.03));

  vec3 finalLighting = albedo.rgb * lighting;

  // ---- Per-fragment fog ----
  float fogDist = u_fogColor.a;
  float fragDist = distance(u_camTime.xyz, v_worldPos);
  float fogFactor = clamp(fragDist / fogDist, 0.0, 1.0);
  fragColor.rgb = mix(finalLighting, u_fogColor.rgb, fogFactor);
  fragColor.a = albedo.a;
}
)glsl";

inline const char* skyVertex = R"glsl(#version 460 core

layout(location = 0) in vec2 a_position;

out vec2 v_screenPos;

void main() {
  v_screenPos = a_position;
  gl_Position = vec4(a_position, 1.0, 1.0);
}
)glsl";

inline const char* skyFragment = R"glsl(#version 460 core

layout(std140, binding = 0) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  mat4 u_invProjView;
  vec4 u_camTime;
  vec4 u_fogColor;
  vec4 u_camRight;
  vec4 u_camUp;
};

layout(std140, binding = 2) uniform TimeBlock {
  float u_timeElapsed;
  float u_daylight;
  float u_sunIntensity;
  float u_ambientIntensity;
  vec3  u_sunDir;
  float u_timeOfDay;
  vec3  u_sunColor;
  float u_pad;
};

in vec2 v_screenPos;
out vec4 fragColor;

void main() {
  vec4 rayClip = vec4(v_screenPos, 1.0, 1.0);
  vec4 rayWorld = u_invProjView * rayClip;
  vec3 viewDir = normalize(rayWorld.xyz / rayWorld.w - u_camTime.xyz);

  // ---- Sky gradient colors ----
  vec3 daySkyTop    = vec3(0.2, 0.5, 1.0);
  vec3 daySkyBot    = vec3(0.6, 0.8, 1.0);
  vec3 nightSkyTop  = vec3(0.005, 0.005, 0.02);
  vec3 nightSkyBot  = vec3(0.01, 0.01, 0.03);
  vec3 sunsetColor  = vec3(0.9, 0.4, 0.15);
  vec3 sunriseColor = vec3(0.85, 0.45, 0.2);

  // ---- Determine if we're near sunrise or sunset ----
  float sunElevation = u_sunDir.y; // -1 below, +1 overhead
  float horizonGlow = exp(-abs(sunElevation) * 8.0); // peaks at horizon

  // ---- Sky gradient ----
  float skyGradient = clamp(viewDir.y * 2.0 + 0.3, 0.0, 1.0);
  vec3 daySky = mix(daySkyBot, daySkyTop, skyGradient);
  vec3 nightSky = mix(nightSkyBot, nightSkyTop, skyGradient);

  // ---- Horizon glow (sunset/sunrise band) ----
  float horizonBand = exp(-abs(viewDir.y) * 12.0);
  // u_sunDir.x = +1 at sunrise (east), -1 at sunset (west)
  float sunriseBlend = u_sunDir.x * 0.5 + 0.5; // 0=west(sunset), 1=east(sunrise)
  vec3 horizonColor = mix(sunsetColor, sunriseColor, sunriseBlend);
  vec3 horizonGlowColor = horizonBand * horizonGlow * horizonColor * u_sunIntensity;

  // ---- Blend day/night sky ----
  vec3 skyColor = mix(nightSky, daySky, u_daylight);
  skyColor += horizonGlowColor;

  // ---- Sun rendering ----
  vec3 sunDir = normalize(u_sunDir);
  float sunDot = max(dot(viewDir, sunDir), 0.0);

  // Sun disc (sharp bright point)
  float sunDisc = pow(sunDot, 1200.0) * u_sunIntensity;
  // Sun glow (soft halo around sun)
  float sunGlow = pow(sunDot, 120.0) * u_sunIntensity * 0.5;
  // Sun corona (very wide faint glow)
  float sunCorona = pow(sunDot, 20.0) * u_sunIntensity * 0.15;

  // @see notes/daynight-sun-tint.md
  vec3 sunRenderColor = u_sunColor * (sunDisc + sunGlow) + sunCorona * vec3(1.0, 0.88, 0.68);

  // ---- Star field (visible at night) ----
  float starIntensity = 1.0 - u_daylight;
  starIntensity = max(0.0, starIntensity * starIntensity - 0.1);
  // Simple pseudo-random stars based on view direction
  vec3 starCoord = viewDir * 200.0;
  float starSeed = fract(sin(dot(starCoord, vec3(12.9898, 78.233, 45.543))) * 43758.5453);
  float star = step(0.997, starSeed) * starIntensity;

  fragColor.rgb = skyColor + sunRenderColor + vec3(star);
  fragColor.a = 1.0;
}
)glsl";

} // namespace voxel::shaders
