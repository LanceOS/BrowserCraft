#version 460 core

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
layout(location = 2) in vec2 a_materialIds;
layout(location = 3) in float a_blend;
layout(location = 4) in float a_tint;

struct ChunkCullData {
    vec4 min;
    vec4 max;
    uint opaqueIndexCount;
    uint opaqueFirstIndex;
    uint transparentIndexCount;
    uint transparentFirstIndex;
    uint baseVertex;
    uint slotIndex;
    uint pad1;
    uint pad2;
    uint renderFlags;
    uint pad3;
    uint pad4;
    uint pad5;
};

layout(std430, binding = 0) readonly buffer InputChunks {
    ChunkCullData chunks[];
};

flat out ivec2 v_materialIds;
out float v_blend;
out float v_tint;
out vec3 v_normal;
out vec3 v_worldPos;

void main() {
  uint chunkIndex = gl_BaseInstance;
  vec3 chunkTranslation = chunks[int(chunkIndex)].min.xyz;
  vec3 worldPos = a_pos + chunkTranslation;
  gl_Position = u_projView * vec4(worldPos, 1.0);

  v_materialIds = ivec2(round(a_materialIds));
  v_blend = clamp(a_blend, 0.0, 1.0);
  v_tint = clamp(a_tint, 0.0, 1.0);
  v_normal = a_normal;
  v_worldPos = worldPos;
}
