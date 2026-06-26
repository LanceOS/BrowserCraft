# Account for SurfaceNets mesher output before allocating mesh space

## What happened
The mesh path used to assume every chunk needed the full worst-case vertex and index budget even though the SurfaceNets mesher usually emits far less geometry.

## Fix
- Add `mesher::estimateMeshCapacity` to compute an upper bound from the current terrain volume.
- Use that hint when reserving mesh memory so chunk uploads can be sized to what the chunk actually needs.

## Related functions
- `mesher::estimateMeshCapacity`
- `mesher::greedyMesh`
- `mesher::calculateLighting`
