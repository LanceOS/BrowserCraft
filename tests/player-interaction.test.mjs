import assert from "node:assert/strict";
import test from "node:test";

import { GameState } from "../dist/engine/core/GameState.js";
import { InputState } from "../dist/engine/core/InputState.js";
import { ComponentStore } from "../dist/engine/ecs/ComponentStore.js";
import { InventoryComponentDesc } from "../dist/engine/ecs/components/InventoryComponent.js";
import { PlayerComponentDesc } from "../dist/engine/ecs/components/PlayerComponent.js";
import { RigidBodyDesc } from "../dist/engine/ecs/components/RigidBody.js";
import { PlayerInteractionController } from "../dist/game/PlayerInteractionController.js";

class FakeHud {
  open = false;
  renders = [];

  constructor(onAction, resolveItemName) {
    this.onAction = onAction;
    this.resolveItemName = resolveItemName;
  }

  dispose() {}

  isOpen() {
    return this.open;
  }

  setInventoryOpen(open) {
    this.open = open;
  }

  render(...args) {
    this.renders.push(args);
  }
}

const createController = () => {
  const input = new InputState();
  const inventories = new ComponentStore(InventoryComponentDesc, 1);
  const players = new ComponentStore(PlayerComponentDesc, 1);
  const bodies = new ComponentStore(RigidBodyDesc, 1);
  const inventoryRow = inventories.add(0);
  const playerRow = players.add(0);
  const bodyRow = bodies.add(0);
  let hud;

  const controller = new PlayerInteractionController(
    input,
    inventories,
    players,
    bodies,
    {},
    {
      tryGet(id) {
        if (id === 58) return { name: "crafting_table" };
        if (id === 5) return { name: "wood_planks" };
        return null;
      },
    },
    {},
    {},
    {},
    {},
    {},
    { position: [0, 0, 0] },
    0,
    (onAction, resolveItemName) => {
      hud = new FakeHud(onAction, resolveItemName);
      return hud;
    },
  );

  return {
    bodyRow,
    bodies,
    controller,
    hud,
    input,
    inventories,
    inventoryBase: inventoryRow * 45,
    players,
    playerRow,
  };
};

test("player interaction toggles inventory and blocks motion input", (t) => {
  const originalDocument = globalThis.document;
  let exitPointerLockCalls = 0;
  globalThis.document = {
    exitPointerLock() {
      exitPointerLockCalls++;
    },
  };
  t.after(() => {
    if (originalDocument === undefined) {
      delete globalThis.document;
    } else {
      globalThis.document = originalDocument;
    }
  });

  const { controller, hud, input } = createController();
  input.setKey("KeyW", true);
  input.mouseDelta[0] = 5;
  input.mouseDelta[1] = -2;

  assert.equal(controller.toggleInventory(), true);

  assert.equal(hud.open, true);
  assert.equal(exitPointerLockCalls, 1);
  assert.equal(input.isHeldCode("KeyW"), false);
  assert.deepEqual(Array.from(input.mouseDelta), [0, 0]);
});

test("player interaction closes inventory outside in-game state and stops player motion", () => {
  const { bodyRow, bodies, controller, hud } = createController();
  hud.open = true;

  bodies.data.velocity[bodyRow * 3 + 0] = 1;
  bodies.data.velocity[bodyRow * 3 + 1] = 2;
  bodies.data.velocity[bodyRow * 3 + 2] = 3;

  controller.syncState(GameState.MAIN_MENU);
  controller.stopPlayerMotion();

  assert.equal(hud.open, false);
  assert.deepEqual(Array.from(bodies.data.velocity.subarray(bodyRow * 3, bodyRow * 3 + 3)), [0, 0, 0]);
});

test("player interaction takes crafting output and consumes the grid", () => {
  const { controller, hud, inventories, inventoryBase } = createController();
  hud.open = true;

  for (let slot = 40; slot <= 43; slot++) {
    inventories.data.itemIds[inventoryBase + slot] = 5;
    inventories.data.itemCounts[inventoryBase + slot] = 1;
  }
  controller.refreshCraftingOutput();
  assert.equal(inventories.data.itemIds[inventoryBase + 44], 58);
  assert.equal(hud.resolveItemName(58), "crafting_table");

  hud.onAction({ slotIndex: 44, button: "left", shiftHeld: false });
  controller.render(GameState.IN_GAME);

  const cursor = hud.renders.at(-1)[2];
  assert.deepEqual(cursor, { itemId: 58, count: 1, metadata: 0 });
  for (let slot = 40; slot <= 44; slot++) {
    assert.equal(inventories.data.itemIds[inventoryBase + slot], 0);
    assert.equal(inventories.data.itemCounts[inventoryBase + slot], 0);
  }
});
