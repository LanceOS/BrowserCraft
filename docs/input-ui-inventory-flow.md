# Input, UI, And Inventory Flow

This document explains how player input moves through the current game loop and how that connects to menus, pointer lock, the inventory HUD, and crafting.

Relevant files:

- [`src/engine/core/InputState.ts`](../src/engine/core/InputState.ts)
- [`src/engine/core/GameLoop.ts`](../src/engine/core/GameLoop.ts)
- [`src/game/PlayerBootstrap.ts`](../src/game/PlayerBootstrap.ts)
- [`src/game/GameSession.ts`](../src/game/GameSession.ts)
- [`src/ui/UIManager.ts`](../src/ui/UIManager.ts)
- [`src/game/PlayerInteractionController.ts`](../src/game/PlayerInteractionController.ts)
- [`src/ui/InventoryHud.ts`](../src/ui/InventoryHud.ts)
- [`src/game/inventory/InventoryControllerSystem.ts`](../src/game/inventory/InventoryControllerSystem.ts)
- [`src/game/inventory/CraftingSystem.ts`](../src/game/inventory/CraftingSystem.ts)
- [`src/engine/ecs/systems/PlayerControllerSystem.ts`](../src/engine/ecs/systems/PlayerControllerSystem.ts)

## Input Ownership

The current stack is intentionally simple:

- DOM events are attached in [`bootstrapPlayerControls()`](../src/game/PlayerBootstrap.ts)
- those handlers mutate [`InputState`](../src/engine/core/InputState.ts)
- systems and controllers read from `InputState` during update
- frame-local transitions are normalized by `InputState.clearFrameState()`

There is no separate command queue or event bus for core player input right now.

## `InputState`

`InputState` tracks:

- `keys` as a small `Uint8Array`
- `mouseDelta` as `Float32Array(2)`
- `mouseButtons` as `Uint8Array(3)`
- `pointerLocked` as a boolean flag

Key state encoding is:

- `0`: up
- `1`: pressed this frame
- `2`: held

Mapped codes currently include:

- movement: `W`, `A`, `S`, `D`
- vertical movement / jump: `Space`, `Shift`
- inventory: `E`
- pause: `Escape`
- hotbar selection: `Digit1` through `Digit9`

`clearFrameState()` demotes `1` to `2` and zeroes mouse delta at the end of each frame.

## Event Wiring

[`bootstrapPlayerControls()`](../src/game/PlayerBootstrap.ts) installs these listeners:

- canvas `click`
- document `pointerlockchange`
- document `mousemove`
- document `keydown`
- document `keyup`
- document `mousedown`
- document `mouseup`

Key behavior:

- clicking the canvas requests pointer lock only when the session is `IN_GAME`
- losing pointer lock clears movement state
- `KeyE` toggles the inventory while in game
- opening inventory exits pointer lock and clears movement
- closing inventory requests pointer lock again

## Game Loop Integration

[`GameLoop`](../src/engine/core/GameLoop.ts) runs a fixed-timestep simulation with a variable render pass.

Updates are only stepped while the session state is:

- `IN_GAME`
- `GENERATING_WORLD`

If the game is in menus or paused:

- the update accumulator is reset
- render still runs every frame

Escape is handled separately inside `GameLoop` by forwarding `toggle-pause` to `UIManager`.

## Session State

[`GameSession`](../src/game/GameSession.ts) is the current authoritative state machine for menu and play transitions.

States:

- `BOOTING`
- `MAIN_MENU`
- `GENERATING_WORLD`
- `IN_GAME`
- `PAUSED`

It also owns:

- the current game mode, `survival` or `creative`
- the clamped render distance
- a monotonically increasing `startRequestId` used to detect new singleplayer starts

Current render distance limits are:

- minimum: `2`
- maximum: `4`

## UI Manager

[`UIManager`](../src/ui/UIManager.ts) renders the menu overlays directly in the DOM.

It manages:

- main menu
- game mode selection
- loading screen
- pause menu
- options menu

The UI is action-driven through `data-action` attributes and a single delegated click handler.

Important current behaviors:

- starting a game routes through `onStartSingleplayer` if the callback is provided
- quitting to title flushes pending saves via the callback provided by `Game`
- render distance changes are written back to `GameSession`
- pausing exits pointer lock

## In-Game Control Flow

During `IN_GAME`, [`Game.update()`](../src/game/Game.ts) does the following input-related work:

1. sync high-level UI state
2. sync hotbar selection from numeric keys
3. reconfigure player state if a new session start was requested
4. if inventory is open, stop player motion and skip ECS system updates
5. otherwise, run the systems
6. update camera, audio, world, save manager, and debug interactions

One subtle but important detail is step 4: when the inventory is open, the current implementation skips `systems.update(this, dt)` entirely. That means opening the inventory suspends more than just player movement.

## Pointer Lock And Movement

[`PlayerControllerSystem`](../src/engine/ecs/systems/PlayerControllerSystem.ts) only processes movement input while `input.pointerLocked` is true.

That means:

- mouse look is disabled when pointer lock is off
- WASD movement is disabled when pointer lock is off
- jumping and flying vertical input are disabled when pointer lock is off

This matches the menu and inventory flow: if a player can see and use the cursor, the world is not currently taking movement input from them.

## Inventory Ownership

Inventory state lives in the ECS through [`InventoryComponentDesc`](../src/engine/ecs/components/InventoryComponent.ts).

The component stores 45 slots worth of:

- `itemIds`
- `itemCounts`
- `itemMetadata`

Current slot usage is:

- `0..8`: hotbar
- `9..35`: main inventory
- `36..39`: currently present in storage/UI, but not given special handling by gameplay systems
- `40..43`: 2x2 player crafting input
- `44`: crafting output

## HUD Rendering

[`InventoryHud`](../src/ui/InventoryHud.ts) owns the DOM for:

- the hotbar
- the inventory panel
- the cursor item label

It does not mutate inventory state directly. Instead it emits `InventoryAction` objects back to [`PlayerInteractionController`](../src/game/PlayerInteractionController.ts).

The HUD is visible only while the session is:

- `IN_GAME`
- `PAUSED`

The inventory panel itself is only visible when `inventoryOpen` is true.

## Inventory Interactions

[`InventoryControllerSystem`](../src/game/inventory/InventoryControllerSystem.ts) handles basic slot logic.

Supported interactions:

- left click pickup/place/swap
- right click split/place-single
- shift click transfer

Shift-click rules currently move between:

- hotbar and main inventory
- crafting area/output and the main inventory range

The cursor item is owned by `PlayerInteractionController`, not by the ECS inventory component.

## Crafting Flow

[`CraftingSystem`](../src/game/inventory/CraftingSystem.ts) evaluates the player's 2x2 crafting grid.

Flow:

1. player changes one of slots `40..43`
2. `PlayerInteractionController` calls `updatePlayerCraftingOutput(...)`
3. the crafting system copies the 2x2 grid into temporary arrays
4. shapeless recipes are checked first
5. shaped recipes are checked next, including mirrored matches
6. slot `44` is updated with the result

When the player takes from the output slot:

- the cursor stack is updated
- one item is consumed from each occupied crafting input slot
- the output is recomputed immediately

## Hotbar Selection

`PlayerInteractionController.syncHotbarSelection()` watches `Digit1` through `Digit9`.

The selected slot is stored in [`PlayerComponentDesc`](../src/engine/ecs/components/PlayerComponent.ts) as `selectedHotbarSlot`, and the HUD highlights the active hotbar slot based on that value.

## Game Modes

`Game` configures the player differently for each mode:

- `creative`: flight enabled, gravity disabled, starter inventory seeded
- `survival`: gravity enabled, no default starter inventory

Creative starter items are currently seeded directly in [`Game.seedCreativeInventory()`](../src/game/Game.ts).

## Debug Interaction Hooks

[`PlayerInteractionController`](../src/game/PlayerInteractionController.ts) also owns the current debug interaction hooks:

- primary mouse button: trigger block break audio and particles at the debug target
- secondary mouse button: toggle a simple redstone rig near the debug target

These only fire while the pointer is locked.

## Practical Gotchas

- Opening the inventory currently exits pointer lock and freezes the ECS update pass for the in-game branch.
- Slots `36..39` exist in the inventory component but are not yet modeled as armor or another dedicated subsystem.
- The cursor item is ephemeral controller state. It is not persisted in the inventory component.
- Render distance is adjusted through the UI, but the actual `config.renderDistance` mutation happens during `Game.update()`.
