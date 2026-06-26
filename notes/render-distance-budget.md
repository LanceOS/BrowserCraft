# Keep render distance within the current memory budget

## What happened
Render distance now gets applied before the runtime stack is built, so the saved value directly affects chunk pool size, renderer buffer allocation, and how many chunks `World::ensureVisibleRadius` tries to keep alive.

With the current `SharedPool` and compact mesh allocator, a large value like `40` still explodes the quadratic chunk budget and tanks startup performance.

## Fix
- Clamp loaded settings through `SaveOrchestrator::loadSettings`.
- Keep `GameSession::MAX_RENDER_DISTANCE` aligned with what the current pool and renderer can support.
- Make the UI read back the clamped value so the menu matches the actual runtime state.
- The current ceiling is `24`, which gives a bit more headroom without reintroducing the old startup spike.

## Related functions
- `voxel::GameOrchestrator::initialize`
- `voxel::GameOrchestrator::buildRuntimeStack`
- `voxel::GameOrchestrator::applyRenderDistance`
- `voxel::SaveOrchestrator::loadSettings`
- `voxel::GameSession::clampRenderDistance`
- `voxel::Renderer::Renderer`
- `voxel::World::ensureVisibleRadius`
