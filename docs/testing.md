# Testing

This repository uses Node's built-in test runner and small focused tests against the compiled `dist/` output.

Relevant files:

- [`package.json`](../package.json)
- [`tests/`](../tests)

## How Tests Run

The current test command is:

```bash
npm run test
```

That script does two things:

1. `npm run build`
2. `node --test tests/*.test.mjs`

The tests import from `dist/`, not from `src/`, so a fresh build is required before the tests run.

## Why Tests Target `dist/`

This keeps the test environment simple:

- no extra test transpiler
- no bundler-specific test harness
- the tests validate the code that the browser would actually execute

The tradeoff is that test failures can come from either:

- TypeScript compile issues
- runtime behavior in the built JavaScript

## Current Test Suites

### `tests/game-session.test.mjs`

Covers:

- state transitions for the singleplayer lifecycle
- render distance clamping
- default game mode behavior

This is the best place to extend if you touch [`GameSession`](../src/game/GameSession.ts) or menu-state transitions.

### `tests/save-format.test.mjs`

Covers:

- chunk save header round-tripping
- payload size validation
- RLE round-tripping
- corrupt/truncated RLE rejection

This suite protects the low-level save format in:

- [`src/engine/save/SaveFormat.ts`](../src/engine/save/SaveFormat.ts)
- [`src/engine/save/RLE.ts`](../src/engine/save/RLE.ts)

### `tests/inventory-controller.test.mjs`

Covers:

- left-click pickup and placement
- right-click stack splitting
- shift-click stack merge and fallback placement

This suite is the first line of defense for:

- [`src/game/inventory/InventoryControllerSystem.ts`](../src/game/inventory/InventoryControllerSystem.ts)

### `tests/crafting-system.test.mjs`

Covers:

- default shapeless recipe matching
- mirrored shaped recipes
- consuming the player crafting grid and refreshing output

This suite exercises:

- [`src/content/crafting/CraftingRegistry.ts`](../src/content/crafting/CraftingRegistry.ts)
- [`src/game/inventory/CraftingSystem.ts`](../src/game/inventory/CraftingSystem.ts)

### `tests/world-remesh.test.mjs`

Covers:

- remesh requeue behavior when edits happen while a chunk is already meshing

This is a narrow but valuable test for chunk lifecycle correctness in:

- [`src/world/World.ts`](../src/world/World.ts)

## Running Individual Suites

After building, you can run one file directly:

```bash
node --test tests/save-format.test.mjs
```

Or rebuild first and then run a specific file:

```bash
npm run build
node --test tests/world-remesh.test.mjs
```

## Adding New Tests

When adding a new suite:

1. create `tests/<feature>.test.mjs`
2. import from `../dist/...`
3. keep fixtures small and deterministic
4. prefer one subsystem per file
5. make the test name explain the behavior, not the implementation detail

## Good Candidates For More Coverage

The current suite is useful, but there are several high-value gaps:

- `SaveWorker` region record behavior and error handling
- `World` load/generate transitions beyond the remesh case
- `RedstoneSystem` propagation rules
- `PlayerInteractionController` inventory/output behavior
- `TimeSystem` daylight and skip-to-morning behavior
- `Renderer`-adjacent pure logic, especially frustum culling or buffer layout helpers

## Practical Tips

- If a test needs browser-only APIs, isolate the pure logic behind a small function first instead of trying to fake the whole runtime.
- If you change serialized data layouts, update tests in the same change.
- If you add new inventory semantics, cover both direct interaction and shift-click behavior.
