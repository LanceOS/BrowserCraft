# Stabilize world slot-to-chunk lookup

## What happened
`World` tracked active chunks by storing raw pointers from `ChunkManager` in `m_slotToChunk`.

`std::unordered_map` iterators and references are invalidated on rehash, and chunk insertion/removal occurs frequently during streaming. This could leave stale pointers and cause generation/meshing callbacks to skip or misroute work, making chunks never transition to `VoxelsReady`/`MeshReady`.

## Fix
- Store chunk coordinates (`chunkX`, `chunkZ`) per slot in `m_slotToChunk` instead of raw `Chunk*` pointers.
- Resolve chunk pointers on demand from coordinates before applying callback/state transitions.
- Validate queue entries by checking that resolved chunk for a slot matches the queued pointer before dispatching generate/mesh tasks.

## Why this matters
This keeps world streaming stable across inserts/removals and prevents invisible worlds caused by dangling `Chunk*` references.
