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
  float u_sunAngle;
  float u_daylight;
  float u_lightLevel;
  vec3 u_sunDir;
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
    uint hasTransparent;
    uint pad1;
    uint pad2;
    uint pad3;
};

layout(std430, binding = 0) readonly buffer InputChunks {
    ChunkCullData chunks[];
};

out vec2 v_uv;
out vec3 v_normal;
out float v_texLayer;
out float v_fogFactor;
out float v_skyLight;
out float v_blockLight;
out float v_ao;

void main() {
  // @see notes/indirect-draw-base-instance.md
  uint chunkIndex = gl_BaseInstance + uint(gl_InstanceID);
  vec3 chunkTranslation = chunks[int(chunkIndex)].min.xyz;
  vec3 worldPos = a_pos + chunkTranslation;
  vec4 clip = u_projView * vec4(worldPos, 1.0);
  gl_Position = clip;

  v_uv = a_uv;
  v_normal = a_normal;
  v_texLayer = a_texLayer;
  uint packedLight = uint(a_lightData + 0.5);
  v_skyLight = float(packedLight & 0x0Fu) / 15.0;
  v_blockLight = float((packedLight >> 4u) & 0x0Fu) / 15.0;
  v_ao = float((packedLight >> 16u) & 0x03u) / 3.0;

  float dist = length(u_camTime.xyz - worldPos);
  float fogDistance = u_fogColor.a;
  v_fogFactor = clamp(dist / fogDistance, 0.0, 1.0);
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
  float u_sunAngle;
  float u_daylight;
  float u_lightLevel;
  vec3 u_sunDir;
  float u_pad;
};

uniform sampler2DArray u_blockTextures;
uniform int u_opaquePass;

in vec2 v_uv;
in vec3 v_normal;
in float v_texLayer;
in float v_fogFactor;
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
  float NdotL = max(dot(normal, sunDir), 0.0);
  float NdotU = max(dot(normal, vec3(0.0, 1.0, 0.0)), 0.0);

  float ambient = 0.05 + 0.15 * NdotU;
  float diffuse = NdotL * u_daylight;
  float minLight = max(v_skyLight * u_daylight * 0.8, v_blockLight * 0.6);

  float light = max(ambient, minLight);
  light = max(light, diffuse * 0.6);
  light = max(light, ambient + (1.0 - ambient) * v_skyLight * u_daylight * 0.25);
  light = mix(0.15, 1.0, light);

  albedo.rgb *= light;
  float aoFactor = clamp(1.0 - v_ao, 0.0, 1.0);
  albedo.rgb *= aoFactor;

  fragColor.rgb = mix(albedo.rgb, u_fogColor.rgb, v_fogFactor);
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
  float u_sunAngle;
  float u_daylight;
  float u_lightLevel;
  vec3 u_sunDir;
  float u_pad;
};

in vec2 v_screenPos;
out vec4 fragColor;

void main() {
  vec4 rayClip = vec4(v_screenPos, 1.0, 1.0);
  vec4 rayWorld = u_invProjView * rayClip;
  vec3 viewDir = normalize(rayWorld.xyz / rayWorld.w - u_camTime.xyz);

  vec3 daySkyTop = vec3(0.2, 0.5, 1.0);
  vec3 daySkyBot = vec3(0.6, 0.8, 1.0);
  vec3 nightSky = vec3(0.01, 0.01, 0.03);
  vec3 duskColor = vec3(0.9, 0.5, 0.2);

  float skyGradient = clamp(viewDir.y * 2.0, 0.0, 1.0);
  vec3 daySky = mix(daySkyBot, daySkyTop, skyGradient);
  vec3 sky = mix(nightSky, daySky, u_daylight);

  vec3 sunDir = normalize(u_sunDir);
  float sunDot = max(dot(viewDir, sunDir), 0.0);
  float sunGlow = pow(sunDot, 600.0) * u_daylight;

  fragColor.rgb = sky + sunGlow * vec3(1.0, 0.9, 0.6);
  fragColor.a = 1.0;
}
)glsl";

} // namespace voxel::shaders
