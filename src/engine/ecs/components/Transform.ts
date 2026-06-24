import type { ComponentDesc } from "../ComponentStore.js";

export const TransformDesc = {
  position: { type: Float32Array, length: 3 },
  rotation: { type: Float32Array, length: 4 },
  scale: { type: Float32Array, length: 3 },
} as const satisfies ComponentDesc;
