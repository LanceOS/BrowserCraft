# Stabilize pending chunk work queues

## What happened
`World` queued pending generation and meshing work as raw `Chunk*` pointers.

Those pointers come from `ChunkManager`, which is backed by `std::unordered_map`. As the world streams in many chunks, new insertions can rehash the map and invalidate previously queued pointers before the queue is drained.

## Fix
- Store pending work as stable `{slotIndex, chunkX, chunkZ}` jobs instead of raw chunk pointers.
- Resolve the live chunk again right before dispatching generation or meshing.
- Reject stale jobs when the slot no longer maps to the same chunk coordinates.

## Why this matters
This keeps chunk generation and meshing alive while the player enters a new world or moves through streaming terrain, instead of silently dropping work after a map rehash.
