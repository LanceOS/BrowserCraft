import assert from "node:assert/strict";
import test from "node:test";

import { CraftingRegistry, createDefaultCraftingRegistry } from "../dist/content/crafting/CraftingRegistry.js";
import { ComponentStore } from "../dist/engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../dist/engine/ecs/components/InventoryComponent.js";
import { CraftingSystem } from "../dist/game/inventory/CraftingSystem.js";

test("crafting system resolves default shapeless recipes", () => {
  const system = new CraftingSystem(createDefaultCraftingRegistry());
  const result = system.evaluateGrid(new Int16Array([17, 0, 0, 0]), new Int16Array(4), 2);
  assert.deepEqual(result, { id: 5, count: 4, metadata: 0 });
});

test("crafting system matches mirrored shaped recipes", () => {
  const registry = new CraftingRegistry();
  registry.registerShaped({
    id: "torch_pair",
    pattern: ["AB"],
    key: { A: 1, B: 2 },
    output: { id: 50, count: 2 },
    gridWidth: 2,
    gridHeight: 1,
  });

  const system = new CraftingSystem(registry);
  const mirroredGrid = new Int16Array([
    2, 1,
    0, 0,
  ]);
  const result = system.evaluateGrid(mirroredGrid, new Int16Array(4), 2);
  assert.deepEqual(result, { id: 50, count: 2, metadata: 0 });
});

test("consumePlayerCraftingGrid decrements ingredients and refreshes the output slot", () => {
  const system = new CraftingSystem(createDefaultCraftingRegistry());
  const inv = new ComponentStore(InventoryComponentDesc, 1);
  const row = inv.add(0);
  const base = row * 45;

  for (let slot = 40; slot <= 43; slot++) {
    inv.data.itemIds[base + slot] = 5;
    inv.data.itemCounts[base + slot] = 1;
  }

  system.updatePlayerCraftingOutput(inv, row);
  assert.equal(inv.data.itemIds[base + 44], 58);
  assert.equal(inv.data.itemCounts[base + 44], 1);

  system.consumePlayerCraftingGrid(inv, row);

  for (let slot = 40; slot <= 43; slot++) {
    assert.equal(inv.data.itemIds[base + slot], 0);
    assert.equal(inv.data.itemCounts[base + slot], 0);
  }
  assert.equal(inv.data.itemIds[base + 44], 0);
  assert.equal(inv.data.itemCounts[base + 44], 0);
});
