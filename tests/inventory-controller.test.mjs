import assert from "node:assert/strict";
import test from "node:test";

import { ComponentStore } from "../dist/engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../dist/engine/ecs/components/InventoryComponent.js";
import { InventoryControllerSystem } from "../dist/game/inventory/InventoryControllerSystem.js";

const createInventory = () => {
  const inv = new ComponentStore(InventoryComponentDesc, 1);
  const row = inv.add(0);
  return { inv, row, base: row * 45 };
};

test("left click picks up and places full stacks", () => {
  const controller = new InventoryControllerSystem();
  const { inv, base } = createInventory();
  const cursor = { itemId: 0, count: 0, metadata: 0 };

  inv.data.itemIds[base + 0] = 5;
  inv.data.itemCounts[base + 0] = 12;

  controller.handleAction(inv, 0, { slotIndex: 0, button: "left", shiftHeld: false }, cursor);
  assert.deepEqual(cursor, { itemId: 5, count: 12, metadata: 0 });
  assert.equal(inv.data.itemIds[base + 0], 0);
  assert.equal(inv.data.itemCounts[base + 0], 0);

  controller.handleAction(inv, 0, { slotIndex: 1, button: "left", shiftHeld: false }, cursor);
  assert.deepEqual(cursor, { itemId: 0, count: 0, metadata: 0 });
  assert.equal(inv.data.itemIds[base + 1], 5);
  assert.equal(inv.data.itemCounts[base + 1], 12);
});

test("right click splits stacks when picking items up", () => {
  const controller = new InventoryControllerSystem();
  const { inv, base } = createInventory();
  const cursor = { itemId: 0, count: 0, metadata: 0 };

  inv.data.itemIds[base + 0] = 4;
  inv.data.itemCounts[base + 0] = 5;

  controller.handleAction(inv, 0, { slotIndex: 0, button: "right", shiftHeld: false }, cursor);
  assert.deepEqual(cursor, { itemId: 4, count: 3, metadata: 0 });
  assert.equal(inv.data.itemCounts[base + 0], 2);
});

test("shift click merges into matching stacks before using empty slots", () => {
  const controller = new InventoryControllerSystem();
  const { inv, base } = createInventory();
  const cursor = { itemId: 0, count: 0, metadata: 0 };

  inv.data.itemIds[base + 0] = 5;
  inv.data.itemCounts[base + 0] = 20;
  inv.data.itemIds[base + 10] = 5;
  inv.data.itemCounts[base + 10] = 60;

  controller.handleAction(inv, 0, { slotIndex: 0, button: "left", shiftHeld: true }, cursor);

  assert.equal(inv.data.itemIds[base + 10], 5);
  assert.equal(inv.data.itemCounts[base + 10], 64);
  assert.equal(inv.data.itemIds[base + 9], 5);
  assert.equal(inv.data.itemCounts[base + 9], 16);
  assert.equal(inv.data.itemIds[base + 0], 0);
  assert.equal(inv.data.itemCounts[base + 0], 0);
  assert.deepEqual(cursor, { itemId: 0, count: 0, metadata: 0 });
});
