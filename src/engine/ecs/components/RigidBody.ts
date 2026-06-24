import type { ComponentDesc } from "../ComponentStore.js";

export const RigidBodyDesc = {
  velocity: { type: Float32Array, length: 3 },
  aabbMin: { type: Float32Array, length: 3 },
  aabbMax: { type: Float32Array, length: 3 },
  onGround: { type: Uint8Array, length: 1 },
  isFluid: { type: Uint8Array, length: 1 },
  gravity: { type: Float32Array, length: 1 },
  drag: { type: Float32Array, length: 1 },
} as const satisfies ComponentDesc;
