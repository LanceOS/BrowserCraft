# Keep the sun close to white at noon

## What happened
The sun disc looked too red because the sun color was being warmed too aggressively even when the sun was high in the sky.

## Fix
- `voxel::daynight::computeSunColor()` now keeps noon close to neutral white and only warms toward sunrise and sunset.
- The sky shader uses a softer corona tint so the sun glow reads as bright light instead of a red blob.

## Related functions
- `voxel::daynight::computeSunColor`
- `voxel::shaders::skyFragment` sun rendering block
