export const particleVertexShaderSource = `#version 300 es
precision highp float;

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

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 i_position;
layout(location = 2) in vec4 i_color;
layout(location = 3) in vec2 i_size_tex;

out vec2 v_uv;
out vec4 v_color;
out float v_texLayer;
out float v_fogFactor;

void main() {
  float scale = i_size_tex.x;
  vec3 billboardOffset =
    (u_camRight.xyz * (a_pos.x - 0.5) + u_camUp.xyz * (a_pos.y - 0.5)) * scale;
  vec3 worldPos = i_position + billboardOffset;
  gl_Position = u_projView * vec4(worldPos, 1.0);

  v_uv = a_pos;
  v_color = i_color;
  v_texLayer = i_size_tex.y;

  float dist = length(u_camTime.xyz - worldPos);
  v_fogFactor = clamp(dist / u_fogColor.a, 0.0, 1.0);
}
`;

export const particleFragmentShaderSource = `#version 300 es
precision highp float;

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

uniform sampler2DArray u_blockTextures;

in vec2 v_uv;
in vec4 v_color;
in float v_texLayer;
in float v_fogFactor;

out vec4 fragColor;

void main() {
  vec4 albedo = texture(u_blockTextures, vec3(v_uv, floor(v_texLayer + 0.5)));
  albedo *= v_color;
  if (albedo.a < 0.1) discard;
  fragColor = vec4(mix(albedo.rgb, u_fogColor.rgb, v_fogFactor), albedo.a);
}
`;
