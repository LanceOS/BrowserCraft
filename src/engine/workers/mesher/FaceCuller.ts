import type { BlockRegistry } from "../../../world/BlockRegistry.js";

export const shouldCullFace = (a: number, b: number, blocks: BlockRegistry): boolean => {
  if (a === b) return true;
  if (a === 0 || b === 0) return false;

  const blockA = blocks.get(a);
  const blockB = blocks.get(b);
  return blockA.material.opaque && blockB.material.opaque;
};
