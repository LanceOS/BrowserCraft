import type { ComponentDesc } from "../ComponentStore.js";

export const MobStatsDesc = {
  width: { type: Float32Array, length: 1 },
  height: { type: Float32Array, length: 1 },
  eyeHeight: { type: Float32Array, length: 1 },
  moveSpeed: { type: Float32Array, length: 1 },
  attackDamage: { type: Float32Array, length: 1 },
  maxHealth: { type: Float32Array, length: 1 },
  modelId: { type: Uint32Array, length: 1 },
} as const satisfies ComponentDesc;
