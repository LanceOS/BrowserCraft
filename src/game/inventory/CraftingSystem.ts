import { CraftingRegistry, type CraftingOutput, type ShapedRecipe } from "../../content/crafting/CraftingRegistry.js";
import type { ComponentStore } from "../../engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../../engine/ecs/components/InventoryComponent.js";

export class CraftingSystem {
  constructor(private readonly registry: CraftingRegistry) {}

  evaluateGrid(gridItems: Int16Array, gridMeta: Int16Array, size: 2 | 3): CraftingOutput | null {
    const shapeless = this.checkShapeless(gridItems);
    if (shapeless) return { id: shapeless.id, count: shapeless.count, metadata: shapeless.metadata ?? 0 };

    const normalized = this.normalizeGrid(gridItems, size);
    if (!normalized) return null;

    const { grid, width, height } = normalized;
    for (const recipe of this.registry.getShaped()) {
      if (recipe.gridWidth !== width || recipe.gridHeight !== height) continue;
      if (this.matchShaped(recipe, grid) || this.matchShapedMirrored(recipe, grid, width)) {
        return { id: recipe.output.id, count: recipe.output.count, metadata: recipe.output.metadata ?? 0 };
      }
    }

    void gridMeta;
    return null;
  }

  updatePlayerCraftingOutput(inv: ComponentStore<typeof InventoryComponentDesc>, row: number): void {
    const base = row * 45;
    const gridItems = new Int16Array(4);
    const gridMeta = new Int16Array(4);
    for (let i = 0; i < 4; i++) {
      gridItems[i] = inv.data.itemIds[base + 40 + i];
      gridMeta[i] = inv.data.itemMetadata[base + 40 + i];
    }

    const result = this.evaluateGrid(gridItems, gridMeta, 2);
    inv.data.itemIds[base + 44] = result?.id ?? 0;
    inv.data.itemCounts[base + 44] = result?.count ?? 0;
    inv.data.itemMetadata[base + 44] = result?.metadata ?? 0;
  }

  consumePlayerCraftingGrid(inv: ComponentStore<typeof InventoryComponentDesc>, row: number): void {
    const base = row * 45;
    for (let i = 0; i < 4; i++) {
      const slot = base + 40 + i;
      if (inv.data.itemCounts[slot] === 0) continue;
      inv.data.itemCounts[slot] -= 1;
      if (inv.data.itemCounts[slot] === 0) {
        inv.data.itemIds[slot] = 0;
        inv.data.itemMetadata[slot] = 0;
      }
    }
    this.updatePlayerCraftingOutput(inv, row);
  }

  private checkShapeless(gridItems: Int16Array): CraftingOutput | null {
    const present: number[] = [];
    for (let i = 0; i < gridItems.length; i++) {
      if (gridItems[i] !== 0) present.push(gridItems[i]);
    }
    if (present.length === 0) return null;
    present.sort((a, b) => a - b);

    for (const recipe of this.registry.getShapeless()) {
      if (recipe.ingredients.length !== present.length) continue;
      const sorted = [...recipe.ingredients].sort((a, b) => a - b);
      let matches = true;
      for (let i = 0; i < sorted.length; i++) {
        if (sorted[i] !== present[i]) {
          matches = false;
          break;
        }
      }
      if (matches) return recipe.output;
    }
    return null;
  }

  private normalizeGrid(gridItems: Int16Array, size: number): { grid: Int16Array; width: number; height: number } | null {
    let minX = size;
    let minY = size;
    let maxX = -1;
    let maxY = -1;

    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        if (gridItems[y * size + x] === 0) continue;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }

    if (maxX === -1) return null;
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
        const expectedId = recipe.key[char] ?? 0;
        if (grid[i] !== expectedId) return false;
        i++;
      }
    }
    return true;
  }

  private matchShapedMirrored(recipe: ShapedRecipe, grid: Int16Array, width: number): boolean {
    let rowBase = 0;
    for (const row of recipe.pattern) {
      for (let x = 0; x < width; x++) {
        const char = row[width - 1 - x];
        const expectedId = recipe.key[char] ?? 0;
        if (grid[rowBase + x] !== expectedId) return false;
      }
      rowBase += width;
    }
    return true;
  }
}
