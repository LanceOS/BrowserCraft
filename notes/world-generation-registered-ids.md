# Fixing World Generation IDs against Asset Registry

## What happened
The world generator could emit block IDs that were not defined in `assets/blocks.json`. Unknown IDs are treated as non-solid in meshing, which can make large parts of generated chunks disappear.

## Root cause
- `BiomeData` and `WorldGenPipeline` emitted IDs outside the default registered set.
- `Desert` biome referenced ID `4` while the JSON initially only contained IDs `1`, `2`, and `3`.
- Ore and cave paths also referenced additional IDs without matching definitions.

## Fix
- Register the generated IDs directly in `assets/blocks.json` so all pipeline IDs resolve to a block definition.
- Update generation constants in `WorldGenPipeline` to use registered block IDs.
- Update desert biome surface/fill IDs to registered values.

## Why this matters
Generated chunks now only use known block definitions, so meshing and collision can reliably produce visible surfaces on first load.
