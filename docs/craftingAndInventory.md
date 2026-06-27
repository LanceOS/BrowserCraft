# Terrain engine Technical Design Document: Inventory & Crafting System



**Version:** 1.0**Scope:** Player Inventory Management, Item Stack Data Structures, UI Interaction State Machine, and Crafting Grid Logic.**Architecture Constraints:** Strict TypeScript, Data-Oriented Design (DOD), Zero-Garbage-Collection on hot paths, ECS integration.


---

## 1. System Overview

The Inventory and Crafting system manages the player's capacity to hold, move, and combine items. To adhere to the engine's Data-Oriented Design philosophy, player inventories are not arrays of `ItemStack` class instances. Instead, they are flat, tightly packed `TypedArray`s stored as an ECS `Component`.

Crafting follows the classic 1.5.2 era mechanics: a 2x2 grid in the player survival inventory, and a 3x3 grid via the Crafting Table block. Recipes are strictly defined as either **Shaped** (positional) or **Shapeless** (combinatorial).


---

## 2. Data Structures (DOD & ECS)

### 2.1 Inventory Component

The inventory is stored as a Structure of Arrays (SoA) within an ECS `ComponentStore`. A standard player inventory has 36 slots (27 main + 9 hotbar), plus 4 armor slots and 4 crafting slots.

```typescript
// /src/engine/ecs/components/InventoryComponent.ts
import { ComponentDesc } from "../ComponentStore";

/**
 * Player Inventory Component (Structure of Arrays).
 * 
 * Slots Layout:
 * 0-8:   Hotbar
 * 9-35:  Main Inventory
 * 36-39: Armor (Boots, Leggings, Chestplate, Helmet)
 * 40-43: Crafting Grid (2x2)
 * 44:    Crafting Output (Read-only)
 * 
 * We use -1 or 0 to represent empty slots.
 */
export const InventoryComponentDesc = {
  // Item IDs (0 = Air/Empty). Int16 supports 32,767 unique items.
  itemIds:      { type: Int16Array, length: 45 },
  // Stack counts (0-64). Uint8 is sufficient.
  itemCounts:   { type: Uint8Array,  length: 45 },
  // Item metadata/damage values (e.g., tool durability, wool color).
  itemMetadata: { type: Int16Array, length: 45 },
} as const satisfies ComponentDesc;
```

### 2.2 The Cursor Item

When the player clicks a slot, the item is "picked up" to the mouse cursor. Since there is only ever one cursor item per player, it is stored as a small, highly localized state object rather than a full ECS component, reducing system iteration overhead.

```typescript
// /src/game/inventory/CursorItem.ts
export interface CursorItem {
  itemId: number;
  count: number;
  metadata: number;
}
```


---

## 3. Inventory Interaction State Machine

All inventory clicks are handled via a strict state machine in the `InventoryControllerSystem`. The DOM `click` / `drag` events are mapped to an `InventoryAction` payload. The system processes these actions against the `InventoryComponent` with O(1) complexity per operation.

### 3.1 Interaction Rules

| Action | Target Slot State | Cursor State | Result |
|:---|:---|:---|:---|
| **Left Click** | Empty | Has Item | Place entire cursor stack into slot. |
| **Left Click** | Has Item | Empty | Pick up entire slot stack to cursor. |
| **Left Click** | Has Item | Has Item (Same ID) | Merge cursor into slot up to Max Stack. Remainder stays on cursor. |
| **Left Click** | Has Item | Has Item (Diff ID) | Swap cursor and slot stacks. |
| **Right Click** | Empty | Has Item | Place 1 item from cursor into slot. |
| **Right Click** | Has Item | Empty | Pick up half of slot (rounded up) to cursor. |
| **Right Click** | Has Item (Same ID) | Has Item | Drop 1 item from cursor into slot. |
| **Shift+Left** | Any | Ignored | Move stack to the "appropriate" section (Hotbar <-> Main, or to/from Crafting grid). |

### 3.2 Implementation Logic

```typescript
// /src/game/inventory/InventoryControllerSystem.ts

export interface InventoryAction {
  readonly slotIndex: number;
  readonly button: "left" | "right";
  readonly shiftHeld: boolean;
}

export class InventoryControllerSystem {
  // Maximum stack size for standard items (1.5.2 era)
  private static readonly MAX_STACK = 64;

  /**
   * Processes an inventory click. O(1) complexity.
   * Mutates the InventoryComponent TypedArrays directly.
   */
  public handleAction(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    playerEntity: number,
    action: InventoryAction,
    cursor: CursorItem
  ): void {
    const row = inv.rowFor(playerEntity);
    if (row === -1) return;

    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;

    const slotIdx = action.slotIndex;
    const slotId = ids[row * 45 + slotIdx];
    const slotCount = counts[row * 45 + slotIdx];

    if (action.shiftHeld) {
      this.handleShiftClick(inv, row, slotIdx, cursor);
      return;
    }

    if (action.button === "left") {
      if (slotId === 0) {
        // Place all
        ids[row * 45 + slotIdx] = cursor.itemId;
        meta[row * 45 + slotIdx] = cursor.metadata;
        counts[row * 45 + slotIdx] = cursor.count;
        cursor.itemId = 0; cursor.count = 0; cursor.metadata = 0;
      } else if (cursor.itemId === slotId) {
        // Merge
        const canAdd = this.MAX_STACK - slotCount;
        const toAdd = Math.min(cursor.count, canAdd);
        if (toAdd > 0) {
          counts[row * 45 + slotIdx] += toAdd;
          cursor.count -= toAdd;
          if (cursor.count === 0) { cursor.itemId = 0; cursor.metadata = 0; }
        } else {
          // Cannot merge (slot full), fallthrough to swap
          this.swap(inv, row, slotIdx, cursor);
        }
      } else {
        // Swap
        this.swap(inv, row, slotIdx, cursor);
      }
    } else if (action.button === "right") {
      if (slotId === 0 && cursor.itemId !== 0) {
        // Place 1
        ids[row * 45 + slotIdx] = cursor.itemId;
        meta[row * 45 + slotIdx] = cursor.metadata;
        counts[row * 45 + slotIdx] = 1;
        cursor.count--;
        if (cursor.count === 0) { cursor.itemId = 0; cursor.metadata = 0; }
      } else if (slotId !== 0 && cursor.itemId === 0) {
        // Pick up half
        const half = Math.ceil(slotCount / 2);
        cursor.itemId = slotId;
        cursor.metadata = meta[row * 45 + slotIdx];
        cursor.count = half;
        counts[row * 45 + slotIdx] = slotCount - half;
        if (counts[row * 45 + slotIdx] === 0) {
          ids[row * 45 + slotIdx] = 0;
        }
      } else if (slotId !== 0 && cursor.itemId === slotId && slotCount < this.MAX_STACK) {
        // Drop 1
        counts[row * 45 + slotIdx]++;
        cursor.count--;
        if (cursor.count === 0) { cursor.itemId = 0; cursor.metadata = 0; }
      }
    }
  }

  private swap(
    inv: ComponentStore<typeof InventoryComponentDesc>,
    row: number, slotIdx: number, cursor: CursorItem
  ): void {
    const ids = inv.data.itemIds;
    const counts = inv.data.itemCounts;
    const meta = inv.data.itemMetadata;

    const tempId = ids[row * 45 + slotIdx];
    const tempCount = counts[row * 45 + slotIdx];
    const tempMeta = meta[row * 45 + slotIdx];

    ids[row * 45 + slotIdx] = cursor.itemId;
    counts[row * 45 + slotIdx] = cursor.count;
    meta[row * 45 + slotIdx] = cursor.metadata;

    cursor.itemId = tempId;
    cursor.count = tempCount;
    cursor.metadata = tempMeta;
  }
  
  // ... handleShiftClick moves items between hotbar (0-8) and main (9-35) ...
}
```


---

## 4. Crafting System Architecture

Crafting logic is entirely decoupled from the inventory UI. A `CraftingRegistry` holds immutable recipe definitions. A `CraftingSystem` evaluates the player's 2x2 or 3x3 grid against the registry whenever the grid contents change.

### 4.1 Recipe Definitions

```typescript
// /src/content/crafting/CraftingRegistry.ts

export interface ShapedRecipe {
  readonly id: string;
  readonly pattern: string[];        // e.g., ["XX", "XX"] for 2x2
  readonly key: Record<string, number>; // e.g., { "X": 5 } (5 = Planks)
  readonly output: { id: number; count: number; metadata?: number };
  readonly gridWidth: number;
  readonly gridHeight: number;
}

export interface ShapelessRecipe {
  readonly id: string;
  readonly ingredients: number[]; // List of item IDs required
  readonly output: { id: number; count: number; metadata?: number };
}

export class CraftingRegistry {
  private shaped: ShapedRecipe[] = [];
  private shapeless: ShapelessRecipe[] = [];

  registerShaped(recipe: ShapedRecipe): void { this.shaped.push(recipe); }
  registerShapeless(recipe: ShapelessRecipe): void { this.shapeless.push(recipe); }
  
  // Getters omitted for brevity
}
```

### 4.2 Grid Matching Algorithm

When a player modifies the crafting grid (slots 40-43), the `CraftingSystem` extracts the grid into a local 2D array, normalizes it (removes empty rows/cols for shaped recipes), and attempts to match it.

```typescript
// /src/game/inventory/CraftingSystem.ts

export class CraftingSystem {
  constructor(private readonly registry: CraftingRegistry) {}

  /**
   * Evaluates the 2x2 or 3x3 grid and returns the resulting item, or null.
   * Called on change. O(R) where R is the number of registered recipes.
   */
  public evaluateGrid(gridItems: Int16Array, gridMeta: Int16Array, size: 2 | 3): { id: number; count: number; metadata: number } | null {
    // 1. Check Shapeless first (easier)
    const shapelessResult = this.checkShapeless(gridItems);
    if (shapelessResult) return shapelessResult;

    // 2. Check Shaped
    // Normalize the grid to find the bounding box of placed items
    const normalized = this.normalizeGrid(gridItems, size);
    if (!normalized) return null;

    const { grid, width, height } = normalized;

    for (const recipe of this.registry.getShaped()) {
      if (recipe.gridWidth === width && recipe.gridHeight === height) {
        if (this.matchShaped(recipe, grid)) {
          return { id: recipe.output.id, count: recipe.output.count, metadata: recipe.output.metadata ?? 0 };
        }
        // Check mirrored pattern (standard Minecraft behavior)
        if (this.matchShapedMirrored(recipe, grid, width)) {
          return { id: recipe.output.id, count: recipe.output.count, metadata: recipe.output.metadata ?? 0 };
        }
      }
    }

    return null;
  }

  private normalizeGrid(gridItems: Int16Array, size: number): { grid: Int16Array, width: number, height: number } | null {
    // Find min/max X and Y that contain items
    let minX = size, minY = size, maxX = -1, maxY = -1;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        if (gridItems[y * size + x] !== 0) {
          if (x < minX) minX = x;
          if (x > maxX) maxX = x;
          if (y < minY) minY = y;
          if (y > maxY) maxY = y;
        }
      }
    }
    
    if (maxX === -1) return null; // Empty grid

    const width = maxX - minX + 1;
    const height = maxY - minY + 1;
    const grid = new Int16Array(width * height);

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        grid[y * width + x] = gridItems[(y + minY) * size + (x + minX)];
      }
    }
    return { grid, width, height };
  }

  private matchShaped(recipe: ShapedRecipe, grid: Int16Array): boolean {
    let i = 0;
    for (const row of recipe.pattern) {
      for (const char of row) {
        const expectedId = recipe.key[char] || 0;
        if (grid[i] !== expectedId) return false;
        i++;
      }
    }
    return true;
  }
  
  private matchShapedMirrored(recipe: ShapedRecipe, grid: Int16Array, width: number): boolean {
    // Reverse the pattern rows horizontally and compare
    let i = 0;
    for (const row of recipe.pattern) {
      for (let x = width - 1; x >= 0; x--) {
        const char = row[x];
        const expectedId = recipe.key[char] || 0;
        if (grid[i + x] !== expectedId) return false;
      }
      i += width;
    }
    return true;
  }
}
```

### 4.3 Crafting Execution Flow


1. **Grid Update:** Player places an item in slots 40-43.
2. **Event Trigger:** `InventoryControllerSystem` detects a change in the crafting area and calls `CraftingSystem.evaluateGrid()`.
3. **Output Population:** The result is written to slot 44 (Crafting Output).
4. **Item Withdrawal:** When the player picks up the item from slot 44, the `InventoryControllerSystem` consumes exactly one ingredient from *each* slot in the 2x2/3x3 grid.
5. **Container Return:** If an ingredient has a container (e.g., Milk Bucket -> Empty Bucket), the consumed slot is replaced with the container item ID instead of being cleared to 0.


---

## 5. UI Rendering & DOM Integration

While the game world is rendered via WebGL2, the inventory UI and hotbar are rendered via a lightweight HTML/DOM or Canvas2D overlay.

* **State Sync:** The UI layer does not hold authoritative state. It reads directly from the `InventoryComponent` TypedArrays every frame (or on dirty-flag tick) to render slots.
* **Input Dispatch:** HTML elements representing slots capture `mousedown` events, map them to an `InventoryAction` payload, and forward them to the `InventoryControllerSystem`.
* **Hotbar Rendering:** The 9 hotbar slots are rendered continuously at the bottom of the screen. The currently selected hotbar slot (0-8) is stored in the `PlayerComponent` and dictates the item held in the player's hand (used for block placement and mob attack calculations).


