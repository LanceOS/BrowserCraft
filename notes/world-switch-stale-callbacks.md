# Guarding world callbacks when switching save slots

## What happened
Switching worlds while asynchronous generation/meshing jobs from the previous world are still running can race with the newly started world.

## Root cause
`World` callbacks (`onSaveLoadSuccess`, `onSaveLoadFailed`, `onWorldGenDone`, `onMeshDone`) were applying results based only on the slot index map.
`slotIndex` alone is reused after `World::clear()`, so old callbacks could overwrite newly created chunks after a world transition.

## Fix
Add state checks in `World` callbacks so late results are only applied when the target chunk is in the state expected for that callback:

- `LoadingFromDisk` for load callbacks
- `Generating` for world-gen completion
- `Meshing` for mesh completion

If the state does not match, the callback is ignored.

## Why this matters
Preventing stale jobs from mutating a freshly selected save slot ensures “Load New World” cannot be contaminated by in-flight work from the previous world.
