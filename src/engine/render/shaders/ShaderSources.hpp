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

layout(location = 0) in uint a_data1;
layout(location = 1) in uint a_data2;

struct ChunkCullData {
    vec4 min;
    vec4 max;
    uint indexCount;
    uint firstIndex;
    uint baseVertex;
    uint slotIndex;
    uint pad1;
    uint pad2;
    uint pad3;
    uint pad4;
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
  uint d1 = a_data1;
  uint d2 = a_data2;

  vec3 a_pos = vec3(
      float(d1 & 0x1Fu),
      float((d1 >> 5u) & 0x1FFu),
      float((d1 >> 14u) & 0x1Fu)
  );
  
  uint normalIdx = (d1 >> 19u) & 0x7u;
  vec3 normals[6] = vec3[6](
      vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0),
      vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0),
      vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0)
  );
  vec3 a_normal = normals[normalIdx];
  
  vec2 a_uv = vec2(float((d1 >> 22u) & 0x1FFu), float(d2 & 0x1FFu));
  float a_texLayer = float((d2 >> 9u) & 0xFFu);
  uint packedLight = (d2 >> 17u) & 0x3FFu;

  vec3 chunkTranslation = chunks[gl_InstanceID].min.xyz;
  vec3 worldPos = a_pos + chunkTranslation;
  vec4 clip = u_projView * vec4(worldPos, 1.0);
  gl_Position = clip;

  v_uv = a_uv;
  v_normal = a_normal;
  v_texLayer = a_texLayer;
  v_skyLight = float(packedLight & 0x0Fu) / 15.0;
  v_blockLight = float((packedLight >> 4u) & 0x0Fu) / 15.0;
  v_ao = float((packedLight >> 8u) & 0x03u) / 3.0;

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

inline const char* cullingCompute = R"glsl(#version 460 core

layout(local_size_x = 64) in;

struct ChunkCullData {
    vec4 min;
    vec4 max;
    uint indexCount;
    uint firstIndex;
    uint baseVertex;
    uint slotIndex;
    uint pad1;
    uint pad2;
    uint pad3;
    uint pad4;
};

struct DrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

layout(std430, binding = 0) readonly buffer InputChunks {
    ChunkCullData chunks[];
};

layout(std430, binding = 1) writeonly buffer OutputCommands {
    DrawCommand commands[];
};

uniform mat4 u_projView;
uniform uint u_maxChunks;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_maxChunks) return;

    ChunkCullData chunk = chunks[idx];
    if (chunk.indexCount == 0) {
        commands[idx].instanceCount = 0;
        return;
    }

    vec4 rowX = vec4(u_projView[0][0], u_projView[1][0], u_projView[2][0], u_projView[3][0]);
    vec4 rowY = vec4(u_projView[0][1], u_projView[1][1], u_projView[2][1], u_projView[3][1]);
    vec4 rowZ = vec4(u_projView[0][2], u_projView[1][2], u_projView[2][2], u_projView[3][2]);
    vec4 rowW = vec4(u_projView[0][3], u_projView[1][3], u_projView[2][3], u_projView[3][3]);

    vec4 planes[6];
    planes[0] = rowW + rowX; // Left
    planes[1] = rowW - rowX; // Right
    planes[2] = rowW + rowY; // Bottom
    planes[3] = rowW - rowY; // Top
    planes[4] = rowW + rowZ; // Near
    planes[5] = rowW - rowZ; // Far

    bool visible = true;
    for (int i = 0; i < 6; i++) {
        vec4 p = planes[i];
        vec3 normal = p.xyz;
        
        vec3 pV = vec3(
            normal.x > 0.0 ? chunk.max.x : chunk.min.x,
            normal.y > 0.0 ? chunk.max.y : chunk.min.y,
            normal.z > 0.0 ? chunk.max.z : chunk.min.z
        );
        
        if (dot(normal, pV) + p.w < 0.0) {
            visible = false;
            break;
        }
    }

    commands[idx].count = chunk.indexCount;
    commands[idx].instanceCount = visible ? 1 : 0;
    commands[idx].firstIndex = chunk.firstIndex;
    commands[idx].baseVertex = chunk.baseVertex;
    commands[idx].baseInstance = chunk.slotIndex;
}
)glsl";

} // namespace voxel::shaders
