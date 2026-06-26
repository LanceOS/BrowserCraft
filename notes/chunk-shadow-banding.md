# Keep voxel sunlight from banding on parallel occluders

## What happened
When several blocks line up in front of the sky, the lighting can turn into visible stripes instead of reading as one blended shadow. The direct sun term was being multiplied by the AO factor, so each AO step darkened the sun itself and made the bands stand out.

## Fix
- Keep AO on the ambient and indirect terms.
- Leave the directional sun contribution unoccluded so parallel faces still read as the same global light source.
- Blend the baked sky/block light channels and soften the AO curve so neighboring occluders read as one merged shadow instead of parallel hard stripes.
- Continue sampling the baked corner light in the mesher so the shader can stay soft instead of inventing new hard edges.

## Related functions
- `voxel::shaders::chunkFragment` lighting block
- `mesher::packLight`
- `mesher::cornerLight`
- `mesher::computeFaceAOPacked`
