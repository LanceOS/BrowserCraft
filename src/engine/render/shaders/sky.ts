export const skyVertexShaderSource = `#version 300 es
precision highp float;

layout(location = 0) in vec2 a_position;

out vec2 v_screenPos;

void main() {
  v_screenPos = a_position;
  gl_Position = vec4(a_position, 1.0, 1.0);
}
`;

export const skyFragmentShaderSource = `#version 300 es
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

layout(std140) uniform TimeBlock {
  float u_timeElapsed;
  float u_sunAngle;
  float u_darkness;
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
  float duskFactor = 1.0 - abs(u_darkness - 0.5) * 2.0;
  duskFactor = smoothstep(0.0, 1.0, duskFactor);

  vec3 finalSky = mix(nightSky, daySky, u_darkness);
  finalSky = mix(finalSky, duskColor, duskFactor * 0.6);

  float sunDot = dot(viewDir, normalize(u_sunDir));
  float sunDisc = smoothstep(0.995, 0.999, sunDot);
  vec3 sunColor = mix(vec3(1.0, 0.6, 0.2), vec3(1.0, 1.0, 0.9), u_darkness);
  finalSky = mix(finalSky, sunColor, sunDisc);

  float moonDot = dot(viewDir, -normalize(u_sunDir));
  float moonDisc = smoothstep(0.995, 0.999, moonDot);
  finalSky = mix(finalSky, vec3(0.8, 0.8, 0.9), moonDisc * (1.0 - u_darkness));

  if (u_darkness < 0.3) {
    float starNoise = fract(sin(dot(viewDir.xz * 100.0, vec2(12.9898, 78.233))) * 43758.5453);
    float star = step(0.998, starNoise) * (1.0 - u_darkness * 3.0);
    finalSky += vec3(star);
  }

  fragColor = vec4(finalSky, 1.0);
}
`;
