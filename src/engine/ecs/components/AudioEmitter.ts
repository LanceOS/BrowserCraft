import type { ComponentDesc } from "../ComponentStore.js";

export const AudioEmitterDesc = {
  cooldown: { type: Float32Array, length: 1 },
  pitch: { type: Float32Array, length: 1 },
  volume: { type: Float32Array, length: 1 },
} as const satisfies ComponentDesc;
