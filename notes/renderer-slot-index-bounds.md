# Guard renderer slot indices before writing chunk buffers

## What happened
Chunk slots can become stale when the world unloads, reloads, or reuses a pool
entry. If the renderer trusts an out-of-range slot index, it can write mesh and
indirect-draw metadata into the wrong buffer region.

## Fix
- Validate `slot.slotIndex` against the indirect batcher capacity before any
  upload or cull-data copy.
- Skip invalid slots instead of writing into the mapped chunk buffers.

## Why this matters
This keeps the renderer from poisoning the shared chunk upload buffers when the
world and render pool briefly disagree about slot ownership.
