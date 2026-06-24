import type { ComponentStore } from "../../engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../../engine/ecs/components/InventoryComponent.js";
import type { CursorItem } from "./CursorItem.js";

export interface InventoryAction {
  readonly slotIndex: number;
  readonly button: "left" | "right";
  readonly shiftHeld: boolean;
}

export class InventoryControllerSystem {
  private static readonly MAX_STACK = 64;

  handleAction(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    playerEntityIndex: number,
    action: InventoryAction,
    cursor: CursorItem,
  ): void {
    const row = inv.rowFor(playerEntityIndex);
    if (row === -1) return;

    if (action.shiftHeld) {
      this.handleShiftClick(inv, row, action.slotIndex);
      return;
    }

    const base = row * 45 + action.slotIndex;
    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;
    const slotId = ids[base];
    const slotCount = counts[base];
    const slotMeta = meta[base];

    if (action.button === "left") {
      if (slotId === 0 && cursor.itemId !== 0) {
        ids[base] = cursor.itemId;
        counts[base] = cursor.count;
        meta[base] = cursor.metadata;
        cursor.itemId = 0;
        cursor.count = 0;
        cursor.metadata = 0;
      } else if (slotId !== 0 && cursor.itemId === 0) {
        cursor.itemId = slotId;
        cursor.count = slotCount;
        cursor.metadata = slotMeta;
        ids[base] = 0;
        counts[base] = 0;
        meta[base] = 0;
      } else if (slotId !== 0 && this.sameStack(slotId, slotMeta, cursor)) {
        const canAdd = InventoryControllerSystem.MAX_STACK - slotCount;
        const toAdd = Math.min(cursor.count, canAdd);
        if (toAdd > 0) {
          counts[base] += toAdd;
          cursor.count -= toAdd;
          if (cursor.count === 0) {
            cursor.itemId = 0;
            cursor.metadata = 0;
          }
        } else {
          this.swap(inv, row, action.slotIndex, cursor);
        }
      } else if (slotId !== 0 || cursor.itemId !== 0) {
        this.swap(inv, row, action.slotIndex, cursor);
      }
      return;
    }

    if (slotId === 0 && cursor.itemId !== 0) {
      ids[base] = cursor.itemId;
      counts[base] = 1;
      meta[base] = cursor.metadata;
      cursor.count -= 1;
      if (cursor.count === 0) {
        cursor.itemId = 0;
        cursor.metadata = 0;
      }
    } else if (slotId !== 0 && cursor.itemId === 0) {
      const half = Math.ceil(slotCount / 2);
      cursor.itemId = slotId;
      cursor.metadata = slotMeta;
      cursor.count = half;
      counts[base] = slotCount - half;
      if (counts[base] === 0) {
        ids[base] = 0;
        meta[base] = 0;
      }
    } else if (slotId !== 0 && this.sameStack(slotId, slotMeta, cursor) && slotCount < InventoryControllerSystem.MAX_STACK) {
      counts[base] += 1;
      cursor.count -= 1;
      if (cursor.count === 0) {
        cursor.itemId = 0;
        cursor.metadata = 0;
      }
    }
  }

  private sameStack(slotId: number, slotMeta: number, cursor: CursorItem): boolean {
    return cursor.itemId !== 0 && slotId === cursor.itemId && slotMeta === cursor.metadata;
  }

  private swap(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    row: number,
    slotIndex: number,
    cursor: CursorItem,
  ): void {
    const base = row * 45 + slotIndex;
    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;

    const tempId = ids[base];
    const tempCount = counts[base];
    const tempMeta = meta[base];

    ids[base] = cursor.itemId;
    counts[base] = cursor.count;
    meta[base] = cursor.metadata;

    cursor.itemId = tempId;
    cursor.count = tempCount;
    cursor.metadata = tempMeta;
  }

  private handleShiftClick(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    row: number,
    slotIndex: number,
  ): void {
    const base = row * 45 + slotIndex;
    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;
    const itemId = ids[base];
    if (itemId === 0) return;

    if (slotIndex <= 8) {
      this.moveStackToRange(inv, row, slotIndex, 9, 35);
    } else if (slotIndex <= 35) {
      this.moveStackToRange(inv, row, slotIndex, 0, 8);
    } else if (slotIndex >= 40 && slotIndex <= 44) {
      this.moveStackToRange(inv, row, slotIndex, 0, 35);
    }

    if (counts[base] === 0) {
      ids[base] = 0;
      meta[base] = 0;
    }
  }

  private moveStackToRange(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    row: number,
    fromSlot: number,
    rangeStart: number,
    rangeEnd: number,
  ): void {
    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;
    const fromBase = row * 45 + fromSlot;
    let remaining = counts[fromBase];
    const itemId = ids[fromBase];
    const itemMeta = meta[fromBase];

    for (let slot = rangeStart; slot <= rangeEnd && remaining > 0; slot++) {
      const base = row * 45 + slot;
      if (ids[base] !== itemId || meta[base] !== itemMeta) continue;
      const canAdd = InventoryControllerSystem.MAX_STACK - counts[base];
      if (canAdd <= 0) continue;
      const moved = Math.min(canAdd, remaining);
      counts[base] += moved;
      remaining -= moved;
    }

    for (let slot = rangeStart; slot <= rangeEnd && remaining > 0; slot++) {
      const base = row * 45 + slot;
      if (ids[base] !== 0) continue;
      const moved = Math.min(InventoryControllerSystem.MAX_STACK, remaining);
      ids[base] = itemId;
      counts[base] = moved;
      meta[base] = itemMeta;
      remaining -= moved;
    }

    counts[fromBase] = remaining;
  }
}
