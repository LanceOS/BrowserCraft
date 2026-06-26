# Clamp chunk-edge light samples

## What happened
Border faces were picking up dark seams because the mesher averaged light samples outside the current chunk as zero when neighbor data was not available.

## Fix
- `mesher::getPackedLight()` now clamps sample coordinates to the chunk volume before sampling.
- `mesher::cornerLight()` stays consistent at chunk edges instead of blending in artificial darkness.

## Related functions
- `mesher::getPackedLight`
- `mesher::cornerLight`
- `mesher::greedyMesh`
