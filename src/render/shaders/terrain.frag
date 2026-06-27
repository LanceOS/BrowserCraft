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

uniform sampler2DArray u_terrainTextures;
uniform int u_opaquePass;

flat in ivec2 v_materialIds;
in float v_blend;
in float v_tint;
in vec3 v_normal;
in vec3 v_worldPos;
in float v_viewSpaceZ;

out vec4 fragColor;

int terrainLayer(int materialId) {
  // Layer indices based on alphabetical sorting of keys in assets/blocks.json
  switch (materialId) {
    case 0: return 8; // grass_top
    case 1: return 4; // dirt
    case 2: return 18; // stone
    case 3: return 17; // sand
    case 4: return 9; // gravel
    case 5: return 0; // clay
    case 6: return 2; // cracked_stone
    default: return 18; // stone
  }
}

float terrainScale(int materialId) {
  switch (materialId) {
    case 0: return 0.18; // grass
    case 1: return 0.16; // dirt
    case 2: return 0.24; // stone
    case 3: return 0.15; // sand
    case 4: return 0.20; // gravel
    case 5: return 0.12; // clay
    case 6: return 0.25; // cracked stone
    default: return 0.18;
  }
}

vec3 terrainTint(int materialId, float tint) {
  float t = clamp(tint, 0.0, 1.0);
  switch (materialId) {
    case 0:
      return mix(vec3(0.82, 0.88, 0.68), vec3(0.72, 0.96, 0.72), t);
    case 1:
      return vec3(0.88, 0.72, 0.54);
    case 2:
      return vec3(0.82, 0.82, 0.84);
    case 3:
      return vec3(0.95, 0.87, 0.68); // sand
    case 4:
      return vec3(0.75, 0.75, 0.75); // gravel
    case 5:
      return vec3(0.90, 0.88, 0.86); // clay
    case 6:
      return vec3(0.75, 0.75, 0.78); // cracked stone
    default:
      return vec3(1.0);
  }
}

vec4 sampleTerrainMaterial(int materialId, vec3 worldPos, vec3 normal, float tint) {
  int layer = terrainLayer(materialId);
  float scale = terrainScale(materialId);

  vec3 weights = abs(normal);
  weights = max(weights, vec3(0.0001));
  weights /= (weights.x + weights.y + weights.z);

  vec2 uvX = worldPos.zy * scale;
  vec2 uvY = worldPos.xz * scale;
  vec2 uvZ = worldPos.xy * scale;

  vec4 texX = texture(u_terrainTextures, vec3(uvX, float(layer)));
  vec4 texY = texture(u_terrainTextures, vec3(uvY, float(layer)));
  vec4 texZ = texture(u_terrainTextures, vec3(uvZ, float(layer)));

  vec4 albedo = texX * weights.x + texY * weights.y + texZ * weights.z;
  albedo.rgb *= terrainTint(materialId, tint);
  return albedo;
}

void main() {
  vec3 normal = normalize(v_normal);
  int primary = v_materialIds.x;
  int secondary = v_materialIds.y;
  float blend = clamp(v_blend, 0.0, 1.0);

  vec4 primaryAlbedo = sampleTerrainMaterial(primary, v_worldPos, normal, v_tint);
  vec4 secondaryAlbedo = sampleTerrainMaterial(secondary, v_worldPos, normal, v_tint);
  vec4 albedo = mix(primaryAlbedo, secondaryAlbedo, blend);

#ifndef OPAQUE_PASS
  if (albedo.a < 0.05) {
    discard;
  }

  if (u_opaquePass == 1 && albedo.a < 0.5) {
    discard;
  }
  if (u_opaquePass == 0 && albedo.a >= 0.5) {
    discard;
  }
#endif

  vec3 sunDir = normalize(u_sunDir);
  float diffuse = max(dot(normal, sunDir), 0.0);
  float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
  float ambient = mix(0.10, u_ambientIntensity * mix(0.18, 0.38, upward), u_daylight);
  vec3 lighting = vec3(ambient) + u_sunColor * (diffuse * u_sunIntensity * 0.90);
  lighting = max(lighting, vec3(0.04));

  vec3 lit = albedo.rgb * lighting;

  float fogDist = u_fogColor.a;
  float fragDist = -v_viewSpaceZ;
  float fogFactor = clamp(fragDist / fogDist, 0.0, 1.0);

  fragColor.rgb = mix(lit, u_fogColor.rgb, fogFactor);
  fragColor.a = albedo.a;
}
