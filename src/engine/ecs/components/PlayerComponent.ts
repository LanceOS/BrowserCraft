import type { ComponentDesc } from "../ComponentStore.js";

export const PlayerComponentDesc = {
  yaw: { type: Float32Array, length: 1 },
  pitch: { type: Float32Array, length: 1 },
  eyeHeight: { type: Float32Array, length: 1 },
  walkSpeed: { type: Float32Array, length: 1 },
  sprintSpeed: { type: Float32Array, length: 1 },
  flySpeed: { type: Float32Array, length: 1 },
  isFlying: { type: Uint8Array, length: 1 },
  selectedHotbarSlot: { type: Uint8Array, length: 1 },
} as const satisfies ComponentDesc;
