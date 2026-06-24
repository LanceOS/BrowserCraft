export const chunkVertexShaderSource = `#version 300 es
precision highp float;
precision highp int;

layout(std140) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  mat4 u_invProjView;
  vec4 u_camTime;
  vec4 u_fogColor;
  vec4 u_camRight;
  vec4 u_camUp;
};

layout(std140) uniform TimeBlock {
  float u_timeElapsed;
  float u_sunAngle;
  float u_darkness;
  float u_lightLevel;
  vec3 u_sunDir;
  float u_pad;
};

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in float a_texLayer;
layout(location = 4) in float a_lightData;

uniform vec3 u_chunkTranslation;

out vec2 v_uv;
out vec3 v_normal;
out float v_texLayer;
out float v_fogFactor;
flat out uint v_packedLight;

void main() {
  vec3 worldPos = a_pos + u_chunkTranslation;
  vec4 clip = u_projView * vec4(worldPos, 1.0);
  gl_Position = clip;

  v_uv = a_uv;
  v_normal = a_normal;
  v_texLayer = a_texLayer;
  v_packedLight = uint(a_lightData + 0.5);

  float dist = length(u_camTime.xyz - worldPos);
  float fogDistance = u_fogColor.a;
  v_fogFactor = clamp(dist / fogDistance, 0.0, 1.0);
}
`;

export const chunkFragmentShaderSource = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;

layout(std140) uniform CameraBlock {
  mat4 u_proj;
  mat4 u_view;
  mat4 u_projView;
  mat4 u_invProjView;
  vec4 u_camTime;
  vec4 u_fogColor;
  vec4 u_camRight;
  vec4 u_camUp;
};

layout(std140) uniform TimeBlock {
  float u_timeElapsed;
  float u_sunAngle;
  float u_darkness;
  float u_lightLevel;
  vec3 u_sunDir;
  float u_pad;
};

uniform sampler2DArray u_blockTextures;

in vec2 v_uv;
in vec3 v_normal;
in float v_texLayer;
in float v_fogFactor;
flat in uint v_packedLight;

out vec4 fragColor;

void main() {
  vec2 tiledUv = fract(v_uv);
  vec4 albedo = texture(u_blockTextures, vec3(tiledUv, floor(v_texLayer + 0.5)));
  if (albedo.a < 0.05) {
    discard;
  }

  vec3 normal = normalize(v_normal);
  float skyLight = float(v_packedLight & 0x0Fu) / 15.0;
  float blockLight = float((v_packedLight >> 4u) & 0x0Fu) / 15.0;
  float ao = float((v_packedLight >> 16u) & 0x03u) / 3.0;
  float effectiveSky = skyLight * (u_lightLevel / 15.0);
  float finalLight = max(effectiveSky, blockLight);
  float diffuse = max(dot(normal, normalize(u_sunDir)), 0.0) * u_darkness * 0.3;
  float aoFactor = mix(0.45, 1.0, ao);
  vec3 lit = albedo.rgb * (finalLight + diffuse + 0.1) * aoFactor;
  vec3 fogColor = mix(vec3(0.0, 0.0, 0.0), u_fogColor.rgb, u_darkness);
  vec3 color = mix(lit, fogColor, v_fogFactor);
  fragColor = vec4(color, albedo.a);
}
`;
