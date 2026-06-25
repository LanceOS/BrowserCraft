# Make per-chunk random generation thread-safe

## What happened
The world generator shared mutable RNG state across worker threads.

`CaveCarver` and `OreDistributor` both kept internal RNG state and were called from the same `WorldGenPipeline` instance on multiple background jobs. That created a data race and could produce empty or corrupted terrain without any explicit crash.

## Fix
- Keep world-scale noise shared and read-only.
- Derive per-chunk RNG state from the chunk seed inside the generation call.
- Pass the chunk seed through the generator so caves and ores stay deterministic per chunk.

## Why this matters
Chunk generation can now run in parallel without corrupting terrain output, and the same world seed still reproduces the same world layout.
