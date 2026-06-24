import type { ComponentDesc } from "../ComponentStore.js";

export const InventoryComponentDesc = {
  itemIds: { type: Int16Array, length: 45 },
  itemCounts: { type: Uint8Array, length: 45 },
  itemMetadata: { type: Int16Array, length: 45 },
} as const satisfies ComponentDesc;
