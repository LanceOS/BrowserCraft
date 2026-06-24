import type { ComponentDesc } from "../ComponentStore.js";

export const HealthDesc = {
  hp: { type: Int16Array, length: 1 },
  maxHp: { type: Int16Array, length: 1 },
  regenCd: { type: Float32Array, length: 1 },
} as const satisfies ComponentDesc;
