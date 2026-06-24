import type { ComponentDesc } from "../ComponentStore.js";

export const AIStateDesc = {
  targetEntity: { type: Uint32Array, length: 1 },
  pathHead: { type: Uint32Array, length: 1 },
  pathLen: { type: Uint32Array, length: 1 },
  state: { type: Uint8Array, length: 1 },
  attackCd: { type: Float32Array, length: 1 },
} as const satisfies ComponentDesc;
